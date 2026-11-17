#pragma once
// Меню для LCD/OLED: навигация, подменю, переключатели и числовые значения.
// Ядро не зависит от дисплея: lineText()/titleText() заполняют буфер,
// а выводом занимается скетч (Lcd1602I2c, OLED и т.п.).
//
// Пример:
//   MenuItem items[] = {
//       MenuItem::Action("Start", onStart),
//       MenuItem::Toggle("LED", &ledOn),
//       MenuItem::Value("Speed", &speed, 0, 255, 5),
//       MenuItem::Submenu("Settings", &settingsPage),
//   };
//   MenuPage mainPage("Main", items, 4);
//   Menu menu;
//   menu.begin(&mainPage);
//   menu.setViewport(2);
//
// Метки можно держать в PROGMEM: последний аргумент фабрик pgm=true,
// либо использовать FN_PGMSTR.

#include <stdint.h>
#include <avr/pgmspace.h>

#include "../core/Features.h"

#if F_USE_LOGIC && F_USE_MENU

#ifndef F_MENU_DEPTH
#define F_MENU_DEPTH 4
#endif

namespace fn {
namespace logic {

enum class MenuItemType : uint8_t { Action, Submenu, Toggle, Value };

struct MenuPage;

struct MenuItem {
    const char* label;
    MenuItemType    kind;
    void (*action)(void*);
    void*       ctx;
    const MenuPage* submenu;
    int16_t*    value;
    bool*       flag;
    int16_t     lo;
    int16_t     hi;
    int16_t     step;
    bool        labelPgm;

    MenuItem()
        : label(nullptr), kind(MenuItemType::Action), action(nullptr), ctx(nullptr),
          submenu(nullptr), value(nullptr), flag(nullptr),
          lo(0), hi(0), step(1), labelPgm(false) {}

    static MenuItem Action(const char* l, void (*fn)(void*),
                           void* c = nullptr, bool pgm = false) {
        MenuItem i;
        i.label = l;
        i.kind = MenuItemType::Action;
        i.action = fn;
        i.ctx = c;
        i.labelPgm = pgm;
        return i;
    }

    static MenuItem Submenu(const char* l, const MenuPage* p, bool pgm = false) {
        MenuItem i;
        i.label = l;
        i.kind = MenuItemType::Submenu;
        i.submenu = p;
        i.labelPgm = pgm;
        return i;
    }

    static MenuItem Toggle(const char* l, bool* f, bool pgm = false) {
        MenuItem i;
        i.label = l;
        i.kind = MenuItemType::Toggle;
        i.flag = f;
        i.labelPgm = pgm;
        return i;
    }

    static MenuItem Value(const char* l, int16_t* v, int16_t lo, int16_t hi,
                          int16_t st = 1, bool pgm = false) {
        MenuItem i;
        i.label = l;
        i.kind = MenuItemType::Value;
        i.value = v;
        i.lo = lo;
        i.hi = hi;
        i.step = (st > 0) ? st : 1;
        i.labelPgm = pgm;
        return i;
    }
};

struct MenuPage {
    const char*     title;
    const MenuItem* items;
    uint8_t         count;
    bool            titlePgm;

    MenuPage(const char* t = nullptr, const MenuItem* i = nullptr,
             uint8_t c = 0, bool pgm = false)
        : title(t), items(i), count(c), titlePgm(pgm) {}
};

class Menu {
   public:
    void begin(const MenuPage* root, void* ctx = nullptr) {
        _stack[0] = root;
        _depth = root ? 1 : 0;
        _cursor = 0;
        _top = 0;
        _editing = false;
        _ctx = ctx;
    }

    // Сколько строк меню показывать (без строки заголовка).
    void setViewport(uint8_t lines) {
        _lines = lines ? lines : uint8_t(1);
        ensureVisible();
    }

    const MenuPage* page() const {
        return _depth ? _stack[_depth - 1] : nullptr;
    }

    uint8_t cursor() const { return _cursor; }
    bool editing() const { return _editing; }
    uint8_t depth() const { return _depth; }
    uint8_t itemCount() const { return page() ? page()->count : 0; }

    const MenuItem* current() const { return itemAt(_cursor); }

    void up() {
        if (_editing) {
            adjust(+1);
            return;
        }
        uint8_t n = itemCount();
        if (!n) return;
        _cursor = (_cursor == 0) ? uint8_t(n - 1) : uint8_t(_cursor - 1);
        ensureVisible();
    }

    void down() {
        if (_editing) {
            adjust(-1);
            return;
        }
        uint8_t n = itemCount();
        if (!n) return;
        _cursor = (_cursor + 1 >= n) ? uint8_t(0) : uint8_t(_cursor + 1);
        ensureVisible();
    }

    // Action — вызвать; Submenu — войти; Toggle — переключить;
    // Value — начать/закончить редактирование.
    bool select() {
        const MenuItem* it = current();
        if (!it) return false;

        if (_editing) {
            if (it->kind == MenuItemType::Value && it->value)
                *it->value = _editValue;
            _editing = false;
            return true;
        }

        switch (it->kind) {
            case MenuItemType::Action:
                if (it->action) it->action(it->ctx ? it->ctx : _ctx);
                return true;

            case MenuItemType::Submenu:
                if (!it->submenu || _depth >= F_MENU_DEPTH) return false;
                _stack[_depth++] = it->submenu;
                _cursor = 0;
                _top = 0;
                return true;

            case MenuItemType::Toggle:
                if (!it->flag) return false;
                *it->flag = !*it->flag;
                return true;

            case MenuItemType::Value:
                if (!it->value) return false;
                _editValue = *it->value;
                if (_editValue < it->lo) _editValue = it->lo;
                if (_editValue > it->hi) _editValue = it->hi;
                _editing = true;
                return true;
        }
        return false;
    }

    // Выход: отмена редактирования -> подменю -> false на корне.
    bool back() {
        if (_editing) {
            _editing = false;
            return true;
        }
        if (_depth > 1) {
            _depth--;
            _cursor = 0;
            _top = 0;
            return true;
        }
        return false;
    }

    void home() {
        if (_depth) _depth = 1;
        _cursor = 0;
        _top = 0;
        _editing = false;
    }

    uint8_t lineCount() const {
        uint8_t n = itemCount();
        return (n > _lines) ? _lines : n;
    }

    // out — RAM-буфер, size — его размер. Строка всегда завершается нулём.
    bool lineText(uint8_t line, char* out, uint8_t size) const {
        if (!out || !size) return false;
        out[0] = '\0';
        if (line >= lineCount()) return false;

        const MenuItem* it = itemAt(uint16_t(_top) + line);
        if (!it) return false;

        char* p = out;
        char* end = out + size - 1;
        bool selected = (uint16_t(_top) + line == _cursor);

        putChar(p, end, selected ? '>' : ' ');
        appendLabel(p, end, it);

        if (it->kind == MenuItemType::Value) {
            appendText(p, end, ": ");
            int32_t v = (_editing && selected && it->value) ? _editValue
                                                            : (it->value ? *it->value : 0);
            appendInt(p, end, v);
        } else if (it->kind == MenuItemType::Toggle) {
            appendText(p, end, (it->flag && *it->flag) ? ": ON" : ": OFF");
        } else if (it->kind == MenuItemType::Submenu) {
            appendText(p, end, " >");
        }

        *p = '\0';
        return true;
    }

    bool titleText(char* out, uint8_t size) const {
        if (!out || !size) return false;
        out[0] = '\0';
        const MenuPage* pg = page();
        if (!pg || !pg->title) return false;
        char* p = out;
        char* end = out + size - 1;
        for (uint16_t i = 0; ; i++) {
            char c = pg->titlePgm ? char(pgm_read_byte(pg->title + i))
                                  : pg->title[i];
            if (!c || p >= end) break;
            *p++ = c;
        }
        *p = '\0';
        return true;
    }

    int16_t value() const {
        const MenuItem* it = current();
        if (!it) return 0;
        if (it->kind == MenuItemType::Value && it->value) return *it->value;
        if (it->kind == MenuItemType::Toggle && it->flag) return *it->flag ? 1 : 0;
        return 0;
    }

    int16_t editValue() const { return _editValue; }

   private:
    const MenuItem* itemAt(uint8_t idx) const {
        const MenuPage* pg = page();
        if (!pg || !pg->items || idx >= pg->count) return nullptr;
        return &pg->items[idx];
    }

    void adjust(int8_t dir) {
        const MenuItem* it = current();
        if (!it || it->kind != MenuItemType::Value) return;
        int32_t v = int32_t(_editValue) + int32_t(it->step) * dir;
        if (v < it->lo) v = it->lo;
        if (v > it->hi) v = it->hi;
        _editValue = int16_t(v);
    }

    void ensureVisible() {
        uint8_t n = itemCount();
        if (!n) {
            _top = 0;
            _cursor = 0;
            return;
        }
        if (_cursor >= n) _cursor = uint8_t(n - 1);
        if (_cursor < _top) _top = _cursor;
        if (uint16_t(_cursor) >= uint16_t(_top) + _lines)
            _top = uint8_t(_cursor - _lines + 1);
        uint8_t maxTop = (n > _lines) ? uint8_t(n - _lines) : uint8_t(0);
        if (_top > maxTop) _top = maxTop;
    }

    static void putChar(char*& p, char* end, char c) {
        if (p < end) *p++ = c;
    }

    static void appendText(char*& p, char* end, const char* s) {
        if (!s) return;
        while (*s) putChar(p, end, *s++);
    }

    static void appendLabel(char*& p, char* end, const MenuItem* it) {
        if (!it || !it->label) return;
        for (uint16_t i = 0; ; i++) {
            char c = it->labelPgm ? char(pgm_read_byte(it->label + i))
                                  : it->label[i];
            if (!c) break;
            putChar(p, end, c);
        }
    }

    static void appendInt(char*& p, char* end, int32_t v) {
        char buf[7];
        uint8_t n = 0;
        bool neg = (v < 0);
        uint32_t uv = neg ? uint32_t(-v) : uint32_t(v);
        if (!uv) buf[n++] = '0';
        while (uv && n < sizeof(buf)) {
            buf[n++] = char('0' + (uv % 10));
            uv /= 10;
        }
        if (neg) putChar(p, end, '-');
        while (n) putChar(p, end, buf[--n]);
    }

    const MenuPage* _stack[F_MENU_DEPTH] = {};
    uint8_t  _depth = 0;
    uint8_t  _cursor = 0;
    uint8_t  _top = 0;
    uint8_t  _lines = 2;
    bool     _editing = false;
    int16_t  _editValue = 0;
    void*    _ctx = nullptr;
};

}  // namespace logic
}  // namespace fn

#endif  // F_USE_LOGIC && F_USE_MENU
