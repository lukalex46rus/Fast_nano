#pragma once
#include "../core/Features.h"
#if F_USE_MENU

#include <Arduino.h>
#include "menu/Builder.h"

namespace fn {
namespace disp {

class DisplayMenu {
   public:
    // столбцов (длина строки), строк
    DisplayMenu(uint8_t cols, uint8_t rows) {
        _menu.cols = cols;
        _menu.rows = rows ? rows : 1;
    }

    // изменить размер меню и вернуться в корень
    void resize(uint8_t cols, uint8_t rows) {
        _menu.cols = cols;
        _menu.rows = rows ? rows : 1;
        _menu.home();
    }

    // подключить рендер вида void(const char* str, size_t len). Придёт nullptr после окончания вывода
    void onPrint(menu::Menu::PrintCallback cb) {
        _menu.printer = cb;
    }

    // установить позицию вывода void(uint8_t col, uint8_t row)
    void onCursor(menu::Menu::CursorCallback cb) {
        _menu.cursor = cb;
    }

    // оформить состояние строки void(uint8_t row, bool selected, bool editing)
    void onState(menu::Menu::StateCallback cb) {
        _menu.state = cb;
    }

    // подключить билдер вида void(menu::Builder& b)
    void onBuild(menu::Builder::BuildCallback cb) {
        _build_cb = cb;
    }

    // размер маркера, сдвигает меню вправо (умолч. 1)
    void setMarkerSize(uint8_t size) {
        _menu.markerSize = size;
    }

    // установить текст кнопки "назад"
    void setBackSign(const char* sign) {
        _menu.backSign = sign;
    }

    // обновить строку с переменной
    void update(const void* var) {
#ifndef MENU_NO_POPUP
        if (_menu.isPopup()) return;
#endif
        if (_build_cb) menu::Builder(_menu).buildUpdate(_build_cb, var);
    }

    // обновить экран
    void refresh() {
        if (_build_cb) menu::Builder(_menu).buildRefresh(_build_cb);
    }

    // на предыдущее меню
    void back() {
#ifndef MENU_NO_POPUP
        if (_menu.isPopup()) return;
#endif
        _menu.back();
        refresh();
    }

    // в главное меню
    void home() {
#ifndef MENU_NO_POPUP
        if (_menu.isPopup()) return;
#endif
        _menu.home();
        refresh();
    }

    // кнопка выбора
    void set() {
        _action(menu::Builder::Action::Set);
    }

    // кнопка вверх
    void up() {
        _menu.active ? _action(menu::Builder::Action::SetUp) : _scroll(true);
    }

    // кнопка вниз
    void down() {
        _menu.active ? _action(menu::Builder::Action::SetDown) : _scroll(false);
    }

    // уменьшить напрямую
    void left() {
        _action(menu::Builder::Action::Left);
    }

    // увеличить напрямую
    void right() {
        _action(menu::Builder::Action::Right);
    }

    // установить режим обновления
    void setRefreshMode(menu::RefreshMode mode) {
        _menu.updateMode = mode;
        _menu.resetPart();
    }

    // обновлять экран полностью
    void setRefreshFull() {
        setRefreshMode(menu::RefreshMode::Full);
    }

    // обновлять изменённые строки целиком
    void setRefreshRow() {
        setRefreshMode(menu::RefreshMode::Row);
    }

    // обновлять только изменяемую часть, если это возможно
    void setRefreshPart() {
        setRefreshMode(menu::RefreshMode::Part);
    }

    // получить текущий номер виджета
    uint8_t currentID() {
        return _menu.currentID();
    }

    // находится ли текущий виджет в режиме редактирования
    bool isEditing() const {
        return _menu.active;
    }

    // выйти из режима редактирования и обновить экран
    void cancelEdit() {
#ifndef MENU_NO_POPUP
        if (_menu.isPopup()) return;
#endif
        if (!_menu.active || !_build_cb) return;
        _menu.active = 0;

        if (_menu.useFull()) {
            refresh();
        } else {
            menu::Builder b(_menu);
            b.buildRow(_build_cb, _menu.row());
            _menu.endRender();
        }
    }

    // установить символ внутреннего маркера (умолч. '>')
    void setMarker(char symb) {
        _menu.marker = symb;
    }

    // получить символ внутреннего маркера
    char getMarker() const {
        return _menu.marker;
    }

    // обновлять экран полностью, например для вывода в консоль (умолч. false)
    void setFullRefresh(bool full) __attribute__((deprecated("Use setRefreshMode() instead."))) {
        setRefreshMode(full ? menu::RefreshMode::Full : menu::RefreshMode::Row);
    }

    // включить быстрый курсор - рендерить только курсор при смене строки (умолч. true)
    void setFastCursor(bool fast) __attribute__((deprecated("Use setRefreshMode() instead."))) {
        setRefreshMode(fast ? menu::RefreshMode::Part : menu::RefreshMode::Row);
    }

    // id текущей страницы
    uint8_t pageID() {
        return _menu.pageID();
    }

    // порядковый номер выбранного виджета
    uint8_t chosenID() {
        return _menu.chosenID();
    }

    // открыт корень меню
    bool isHome() {
        return _menu.isHome();
    }

#ifndef MENU_NO_POPUP
    // открыть popup из внешнего кода. id 0 зарезервирован
    bool openPopup(uint8_t id, uint8_t selected = 0) {
        bool alreadyOpen = _menu.popup.id == id;
        if (!_build_cb || !_menu.popup.open(id, selected)) return false;
        if (alreadyOpen) return true;
        _menu.resetPart();
        refresh();
        return true;
    }

    // закрыть popup без результата
    bool closePopup() {
        if (!_menu.isPopup()) return false;
        _menu.popup.close();
        _menu.resetPart();
        refresh();
        return true;
    }

    bool isPopup() const {
        return _menu.isPopup();
    }

    uint8_t popupID() const {
        return _menu.popupID();
    }
#endif

    // MARK: private
   private:
    menu::Menu _menu;
    menu::Builder::BuildCallback _build_cb = nullptr;

    // обработать действие текущего виджета
    void _action(menu::Builder::Action action) {
        if (!_build_cb) return;

        menu::Builder b(_menu);
        b.buildAction(_build_cb, action, _menu.useFull());
        if (b.isRefresh()) b.buildRefresh(_build_cb);  // + end render
        else _menu.endRender();
    }

    // переместить выбранную строку и обновить экран по текущему режиму
    void _scroll(bool up) {
        if (!_build_cb) return;

#ifndef MENU_NO_POPUP
        if (_menu.isPopup()) {
            _action(up ? menu::Builder::Action::SetUp : menu::Builder::Action::SetDown);
            return;
        }
#endif

        uint8_t prow = _menu.row();

        if (_menu.move(up)) {
            refresh();
        } else if (prow != _menu.row()) {
            switch (_menu.updateMode) {
                case menu::RefreshMode::Part:
                    _menu.setCursor(prow, false);
                    _menu.setCursor(_menu.row(), true);
                    break;

                case menu::RefreshMode::Row: {
                    menu::Builder b(_menu);
                    b.buildRow(_build_cb, prow);
                    b.buildRow(_build_cb, _menu.row());
                } break;

                case menu::RefreshMode::Full:
                    refresh();
                    return;
            }

            _menu.endRender();
        }
    }
};

}  // namespace disp
}  // namespace fn

#endif  // F_USE_MENU
