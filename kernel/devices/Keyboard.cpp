#include <abi/Keyboard.h>
#include <abi/Paths.h>

#include <libsystem/Assert.h>
#include <libsystem/Logger.h>
#include <libsystem/Result.h>
#include <libsystem/core/CString.h>
#include <libsystem/thread/Atomic.h>
#include <libutils/RingBuffer.h>

#include "arch/x86/PS2.h"
#include "arch/x86/x86.h"
#include "kernel/Configs.h"
#include "kernel/filesystem/Filesystem.h"
#include "kernel/interrupts/Dispatcher.h"

/* --- Private functions ---------------------------------------------------- */

#define PS2KBD_ESCAPE 0xE0

enum PS2KeyboardState
{
    PS2KBD_STATE_NORMAL,
    PS2KBD_STATE_ESCAPED,
};

static PS2KeyboardState _state = PS2KBD_STATE_NORMAL;
static KeyMotion _keystate[__KEY_COUNT] = {};
static KeyMap *_keymap = nullptr;

static FsNode *_characters_node = nullptr;
static RingBuffer *_characters_buffer = nullptr;

static FsNode *_events_node = nullptr;
static RingBuffer *_events_buffer = nullptr;

Codepoint keyboard_get_codepoint(Key key)
{
    KeyMapping *mapping = keymap_lookup(_keymap, key);

    if (mapping == nullptr)
    {
        return 0;
    }

    if (_keystate[KEYBOARD_KEY_LSHIFT] == KEY_MOTION_DOWN ||
        _keystate[KEYBOARD_KEY_RSHIFT] == KEY_MOTION_DOWN)
    {
        return mapping->shift_codepoint;
    }
    else if (_keystate[KEYBOARD_KEY_RALT] == KEY_MOTION_DOWN)
    {
        return mapping->alt_codepoint;
    }
    else
    {
        return mapping->regular_codepoint;
    }
}

KeyModifier keyboard_get_modifiers()
{
    KeyModifier modifiers = 0;

    if (_keystate[KEYBOARD_KEY_LALT] == KEY_MOTION_DOWN)
    {
        modifiers |= KEY_MODIFIER_ALT;
    }

    if (_keystate[KEYBOARD_KEY_RALT] == KEY_MOTION_DOWN)
    {
        modifiers |= KEY_MODIFIER_ALTGR;
    }

    if (_keystate[KEYBOARD_KEY_LSHIFT] == KEY_MOTION_DOWN ||
        _keystate[KEYBOARD_KEY_RSHIFT] == KEY_MOTION_DOWN)
    {
        modifiers |= KEY_MODIFIER_SHIFT;
    }

    if (_keystate[KEYBOARD_KEY_LCTRL] == KEY_MOTION_DOWN ||
        _keystate[KEYBOARD_KEY_RCTRL] == KEY_MOTION_DOWN)
    {
        modifiers |= KEY_MODIFIER_CTRL;
    }

    if (_keystate[KEYBOARD_KEY_LSUPER] == KEY_MOTION_DOWN ||
        _keystate[KEYBOARD_KEY_RSUPER] == KEY_MOTION_DOWN)
    {
        modifiers |= KEY_MODIFIER_SUPER;
    }

    return modifiers;
}

void keyboard_handle_key(Key key, KeyMotion motion)
{
    if (!key_is_valid(key))
    {
        logger_warn("Invalid scancode %d", key);
        return;
    }

    Codepoint codepoint = keyboard_get_codepoint(key);

    if (_characters_node->readers)
    {
        if (motion == KEY_MOTION_DOWN)
        {
            if (codepoint != 0)
            {
                uint8_t utf8[5];
                int length = codepoint_to_utf8(codepoint, utf8);

                if (_characters_node->readers)
                    _characters_buffer->write((const char *)utf8, length);
            }
        }
    }

    if (_events_node->readers)
    {
        if (_keystate[key] == KEY_MOTION_UP && motion == KEY_MOTION_DOWN)
        {
            KeyboardPacket packet = {
                key,
                keyboard_get_modifiers(),
                codepoint,
                KEY_MOTION_DOWN,
            };

            _events_buffer->write((char *)&packet, sizeof(KeyboardPacket));
        }

        if (motion == KEY_MOTION_UP)
        {
            KeyboardPacket packet = {
                key,
                keyboard_get_modifiers(),
                codepoint,
                KEY_MOTION_UP,
            };

            _events_buffer->write((char *)&packet, sizeof(KeyboardPacket));
        }

        if (motion == KEY_MOTION_DOWN)
        {
            KeyboardPacket packet = {
                key,
                keyboard_get_modifiers(),
                codepoint,
                KEY_MOTION_TYPED,
            };

            _events_buffer->write((char *)&packet, sizeof(KeyboardPacket));
        }
    }

    _keystate[key] = motion;
}

Key keyboard_scancode_to_key(int scancode)
{
#define SCAN_CODE_TO_KEY_ENTRY(__key_name, __key_number) \
    if (scancode == (__key_number))                      \
        return __key_name;

    KEY_LIST(SCAN_CODE_TO_KEY_ENTRY);

    return KEYBOARD_KEY_INVALID;
}

void keyboard_interrupt_handler()
{
    AtomicHolder holder;

    uint8_t status = in8(PS2_STATUS);

    while (((status & PS2_WHICH_BUFFER) == PS2_KEYBOARD_BUFFER) &&
           (status & PS2_BUFFER_FULL))
    {
        uint8_t scancode = in8(PS2_BUFFER);

        if (_state == PS2KBD_STATE_NORMAL)
        {
            if (scancode == PS2KBD_ESCAPE)
            {
                _state = PS2KBD_STATE_ESCAPED;
            }
            else
            {
                Key key = keyboard_scancode_to_key(scancode & 0x7F);
                keyboard_handle_key(key, scancode & 0x80 ? KEY_MOTION_UP : KEY_MOTION_DOWN);
            }
        }
        else if (_state == PS2KBD_STATE_ESCAPED)
        {
            _state = PS2KBD_STATE_NORMAL;

            Key key = keyboard_scancode_to_key((scancode & 0x7F) + 0x80);
            keyboard_handle_key(key, scancode & 0x80 ? KEY_MOTION_UP : KEY_MOTION_DOWN);
        }

        status = in8(PS2_STATUS);
    }
}

/* --- Public functions ----------------------------------------------------- */

KeyMap *keyboard_load_keymap(const char *keymap_path)
{
    __cleanup(stream_cleanup) Stream *keymap_file = stream_open(keymap_path, OPEN_READ);

    if (handle_has_error(keymap_file))
    {
        logger_error("Failed to load keymap from %s: %s", keymap_path, handle_error_string(keymap_file));

        return nullptr;
    }

    FileState stat;
    stream_stat(keymap_file, &stat);

    if (stat.type != FILE_TYPE_REGULAR)
    {
        logger_info("Failed to load keymap from %s: This is not a regular file", keymap_path);

        return nullptr;
    }

    logger_info("Allocating keymap of size %dkio", stat.size / 1024);
    KeyMap *keymap = (KeyMap *)malloc(stat.size);

    size_t read = stream_read(keymap_file, keymap, stat.size);

    if (read != stat.size)
    {
        logger_error("Failed to load keymap from %s: %s", keymap_path, handle_error_string(keymap_file));

        free(keymap);

        return nullptr;
    }

    return keymap;
}

static Result keyboard_iocall(FsNode *node, FsHandle *handle, IOCall request, void *args)
{
    __unused(node);
    __unused(handle);

    if (request == IOCALL_KEYBOARD_SET_KEYMAP)
    {
        IOCallKeyboardSetKeymapArgs *size_and_keymap = (IOCallKeyboardSetKeymapArgs *)args;
        KeyMap *new_keymap = (KeyMap *)size_and_keymap->keymap;

        AtomicHolder holder;

        if (_keymap != nullptr)
        {
            free(_keymap);
        }

        _keymap = (KeyMap *)malloc(size_and_keymap->size);
        memcpy(_keymap, new_keymap, size_and_keymap->size);

        return SUCCESS;
    }
    else if (request == IOCALL_KEYBOARD_GET_KEYMAP)
    {
        memcpy(args, _keymap, sizeof(KeyMap));
        return SUCCESS;
    }
    else
    {
        return ERR_INAPPROPRIATE_CALL_FOR_DEVICE;
    }
}

class Keyboard : public FsNode
{
private:
    /* data */
public:
    Keyboard() : FsNode(FILE_TYPE_DEVICE)
    {
        call = (FsNodeCallCallback)keyboard_iocall;
    }

    ~Keyboard() {}

    bool can_read(FsHandle *handle)
    {
        __unused(handle);

        // FIXME: make this atomic or something...
        return !_characters_buffer->empty();
    }

    ResultOr<size_t> read(FsHandle &handle, void *buffer, size_t size)
    {
        __unused(handle);

        AtomicHolder holder;

        // FIXME: use locks
        return _characters_buffer->read((char *)buffer, size);
    }
};

class KeyboardEvent : public FsNode
{
private:
public:
    KeyboardEvent() : FsNode(FILE_TYPE_DEVICE)
    {
        call = (FsNodeCallCallback)keyboard_iocall;
    }

    ~KeyboardEvent()
    {
    }

    bool can_read(FsHandle *handle)
    {
        __unused(handle);

        // FIXME: make this atomic or something...
        return !_events_buffer->empty();
    }

    ResultOr<size_t> read(FsHandle &handle, void *buffer, size_t size)
    {
        __unused(handle);

        AtomicHolder holder;

        return _events_buffer->read((char *)buffer, (size / sizeof(KeyboardPacket)) * sizeof(KeyboardPacket));
    }
};

void keyboard_initialize()
{
    logger_info("Initializing keyboard...");

    _keymap = keyboard_load_keymap("/System/Keyboards/" CONFIG_KEYBOARD_LAYOUT ".kmap");

    _characters_buffer = new RingBuffer(1024);
    _characters_node = new Keyboard();

    filesystem_link_cstring(KEYBOARD_DEVICE_PATH, _characters_node);

    _events_buffer = new RingBuffer(sizeof(KeyboardPacket) * 256);
    _events_node = new KeyboardEvent();

    filesystem_link_cstring(KEYBOARD_EVENT_DEVICE_PATH, _events_node);

    dispatcher_register_handler(1, keyboard_interrupt_handler);
}
