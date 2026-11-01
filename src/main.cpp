#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <chrono>

using namespace geode::prelude;

// ===========================================================================
// Перечисления и утилиты
// ===========================================================================
enum class Mode : int { Cube=0, Ship=1, Ball=2, UFO=3, Wave=4, Robot=5, Spider=6, Swing=7, Unknown=-1 };
enum class ClickType { Landing, Orb, MinHold, MinGap, Grid, Unknown };

namespace {
    inline Mode getMode(PlayerObject* p) {
        if (!p) return Mode::Unknown;
        int g = static_cast<int>(p->m_gameMode);
        return (g >= 0 && g <= 7) ? static_cast<Mode>(g) : Mode::Unknown;
    }

    inline bool masterEnabled() { return Mod::get()->getSettingValue<bool>("master-enable"); }
    inline void dbg(const std::string& msg) { if (Mod::get()->getSettingValue<bool>("log")) log::info("[UDMURT] {}", msg); }

    inline bool isModeEnabled(Mode m) {
        auto mod = Mod::get();
        switch (m) {
            case Mode::Cube: return mod->getSettingValue<bool>("mode-cube");
            case Mode::Ship: return mod->getSettingValue<bool>("mode-ship");
            case Mode::Ball: return mod->getSettingValue<bool>("mode-ball");
            case Mode::UFO: return mod->getSettingValue<bool>("mode-ufo");
            case Mode::Wave: return mod->getSettingValue<bool>("mode-wave");
            case Mode::Robot: return mod->getSettingValue<bool>("mode-robot");
            case Mode::Spider: return mod->getSettingValue<bool>("mode-spider");
            case Mode::Swing: return mod->getSettingValue<bool>("mode-swing");
            default: return true;
        }
    }

    inline bool isClickTypeEnabled(ClickType t) {
        auto mod = Mod::get();
        switch (t) {
            case ClickType::Landing: return mod->getSettingValue<bool>("click-landing");
            case ClickType::Orb: return mod->getSettingValue<bool>("click-orb");
            case ClickType::MinHold: return mod->getSettingValue<bool>("click-min-hold");
            case ClickType::MinGap: return mod->getSettingValue<bool>("click-min-gap");
            case ClickType::Grid: return mod->getSettingValue<bool>("click-grid");
            default: return true;
        }
    }

    ClickType inferClickType(PlayerObject* p) {
        if (!p) return ClickType::Unknown;
        if (p->m_touchingRings && p->m_touchingRings->count() > 0) return ClickType::Orb;
        auto m = getMode(p);
        if (m == Mode::Wave || m == Mode::Ship || m == Mode::UFO || m == Mode::Swing) {
            return ClickType::MinHold; 
        }
        return ClickType::Landing;
    }

    inline int getWindowFor(ClickType t) {
        auto mod = Mod::get();
        switch (t) {
            case ClickType::Landing: return mod->getSettingValue<int>("landing-window");
            case ClickType::Orb: return mod->getSettingValue<int>("orb-window");
            case ClickType::MinHold: return mod->getSettingValue<int>("min-hold-window");
            case ClickType::MinGap: return mod->getSettingValue<int>("min-gap-window");
            case ClickType::Grid: return mod->getSettingValue<int>("grid-window");
            default: return 0;
        }
    }

    inline float toSeconds(int value) {
        auto mod = Mod::get();
        bool frames = mod->getSettingValue<std::string>("unit") == "frames";
        if (frames) {
            int fps = mod->getSettingValue<int>("fps");
            if (fps < 1) fps = 60;
            return static_cast<float>(value) / static_cast<float>(fps);
        }
        return static_cast<float>(value) / 240.0f;
    }

    inline bool shouldBuffer(PlayerObject* p) {
        if (!masterEnabled()) return false;
        if (!isModeEnabled(getMode(p))) return false;
        if (!isClickTypeEnabled(inferClickType(p))) return false;
        int win = getWindowFor(inferClickType(p));
        return win > 0;
    }

    inline cocos2d::ccColor3B colorBlack() { return {0, 0, 0}; }
    inline cocos2d::ccColor3B colorRed() { return {200, 30, 30}; }
    inline cocos2d::ccColor3B colorWhite() { return {255, 255, 255}; }
}

// ===========================================================================
// Кастомное окно настроек
// ===========================================================================
class UdmurtSettingsPopup : public geode::Popup {
protected:
    std::unordered_map<int, std::string> m_settingIds;
    bool init() override {
        if (!geode::Popup::init(420.f, 360.f)) return false;
        this->getBG()->setColor(colorBlack());
        this->getBG()->setOpacity(230);
        this->setTitle("UDMURT BUFFERS");

        auto scroll = ScrollLayer::create({380.f, 280.f});
        scroll->setPosition({20.f, 30.f});
        m_mainLayer->addChild(scroll);
        auto content = scroll->m_contentLayer;
        float y = 0.f; float lineH = 28.f;

        auto addLabel = [&](const char* text) {
            auto lbl = CCLabelBMFont::create(text, "goldFont.fnt");
            lbl->setColor(colorRed()); lbl->setScale(0.8f);
            lbl->setPosition({190.f, y}); content->addChild(lbl);
            y -= lineH * 1.5f;
        };

        addLabel("MODES");
        const char* modes[][2] = {{"mode-cube","Cube"},{"mode-ship","Ship"},{"mode-ball","Ball"},{"mode-ufo","UFO"},{"mode-wave","Wave"},{"mode-robot","Robot"},{"mode-spider","Spider"},{"mode-swing","Swing"}};
        for (auto& m : modes) { addToggle(content, m[0], m[1], y); y -= lineH; }

        addLabel("CLICK TYPES");
        const char* clicks[][2] = {{"click-landing","Landing"},{"click-orb","Orb"},{"click-min-hold","Min Hold"},{"click-min-gap","Min Gap"},{"click-grid","Grid"}};
        for (auto& c : clicks) { addToggle(content, c[0], c[1], y); y -= lineH; }

        y -= lineH * 0.5f;
        addToggle(content, "master-enable", "MASTER ENABLE", y);
        y -= lineH;

        content->setContentSize({380.f, std::abs(y) + 20.f});
        content->setPositionY(scroll->getContentSize().height - content->getContentSize().height);
        scroll->updateAllPages();
        return true;
    }

    void addToggle(CCNode* parent, const char* settingId, const char* labelText, float y) {
        auto lbl = CCLabelBMFont::create(labelText, "bigFont.fnt");
        lbl->setColor(colorWhite()); lbl->setScale(0.55f);
        lbl->setAnchorPoint({0.f, 0.5f}); lbl->setPosition({10.f, y});
        parent->addChild(lbl);

        auto offSpr = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
        offSpr->setColor(colorBlack());
        auto onSpr = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
        onSpr->setColor(colorRed());

        auto toggle = CCMenuItemToggler::create(offSpr, onSpr, this, menu_selector(UdmurtSettingsPopup::onToggle));
        int tag = hashSettingId(settingId);
        m_settingIds[tag] = settingId;
        toggle->setTag(tag); toggle->setPosition({360.f, y});
        toggle->toggle(Mod::get()->getSettingValue<bool>(settingId));

        auto menu = CCMenu::create(); menu->addChild(toggle);
        menu->setPosition({0.f, 0.f}); parent->addChild(menu);
    }

    int hashSettingId(const char* id) { int h = 0; for (const char* p = id; *p; ++p) h = h * 31 + *p; return h; }
    void onToggle(CCObject* sender) {
        auto toggle = static_cast<CCMenuItemToggler*>(sender);
        auto it = m_settingIds.find(toggle->getTag());
        if (it != m_settingIds.end()) {
            Mod::get()->setSettingValue<bool>(it->second, toggle->isToggled());
        }
    }
public:
    static UdmurtSettingsPopup* create() {
        auto ret = new UdmurtSettingsPopup();
        if (ret->init()) { ret->autorelease(); return ret; }
        delete ret; return nullptr;
    }
};

// ===========================================================================
// ХУКИ: Игрок
// ===========================================================================
class $modify(UDMURTPlayer, PlayerObject) {
    struct Fields {
        bool hasPendingClick = false;
        bool isHoldingBuffered = false;
        std::chrono::steady_clock::time_point clickTime;
        float bufferWindowSeconds = 0.f;
        PlayerButton pendingButton;
    };

    void pushButton(PlayerButton button) {
        if (!shouldBuffer(this)) {
            // Буфер выключен: играем как обычно
            PlayerObject::pushButton(button);
            return;
        }

        // Буфер включен: блокируем клик и ждем
        auto t = inferClickType(this);
        int win = getWindowFor(t);
        m_fields->bufferWindowSeconds = toSeconds(win);
        m_fields->hasPendingClick = true;
        m_fields->pendingButton = button;
        m_fields->clickTime = std::chrono::steady_clock::now();
        dbg(fmt::format("Buffered push. Waiting {}s", m_fields->bufferWindowSeconds));
    }

    void releaseButton(PlayerButton button) {
        // Если у нас висит клик или мы эмулируем удержание — игнорируем
        if (m_fields->hasPendingClick || m_fields->isHoldingBuffered) return;
        PlayerObject::releaseButton(button);
    }

    void update(float dt) {
        PlayerObject::update(dt);

        // 1. Проверка висящего клика
        if (m_fields->hasPendingClick) {
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - m_fields->clickTime).count();
            bool shouldFire = false;
            auto mode = getMode(this);

            if (mode == Mode::Cube || mode == Mode::Ball || mode == Mode::Robot || mode == Mode::Spider) {
                if (this->m_isOnGround) shouldFire = true;
            }
            
            if (elapsed >= m_fields->bufferWindowSeconds) shouldFire = true;

            if (shouldFire) {
                dbg("Firing buffered push!");
                PlayerObject::pushButton(m_fields->pendingButton);
                m_fields->isHoldingBuffered = true;
                m_fields->hasPendingClick = false;
                m_fields->clickTime = now;
            }
        }

        // 2. Проверка эмуляции удержания (Min Hold)
        if (m_fields->isHoldingBuffered) {
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - m_fields->clickTime).count();
            
            // Держим кнопку минимум 1 кадр (0.016с), чтобы игра засчитала нажатие
            float holdTime = std::max(0.016f, m_fields->bufferWindowSeconds); 
            
            if (elapsed >= holdTime) {
                dbg("Releasing buffered hold!");
                PlayerObject::releaseButton(m_fields->pendingButton);
                m_fields->isHoldingBuffered = false;
            }
        }
    }
};

// ===========================================================================
// ХУКИ: Уровень
// ===========================================================================
class $modify(UDMURTLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        // Сбрасываем застрявшие клики при смерти
        auto resetPlayer = [](PlayerObject* p) {
            if (!p) return;
            auto mod = static_cast<UDMURTPlayer*>(p);
            if (mod->m_fields) {
                mod->m_fields->hasPendingClick = false;
                mod->m_fields->isHoldingBuffered = false;
            }
        };
        resetPlayer(this->m_player1);
        resetPlayer(this->m_player2);
    }
};

// ===========================================================================
// Точка входа
// ===========================================================================
$execute {
    ButtonSettingPressedEventV3(Mod::get(), "open-ui").listen([](auto buttonKey) {
        if (buttonKey == "open") UdmurtSettingsPopup::create()->show();
    }).leak();
    log::info("UDMURT Buffers v2.1.0 loaded.");
}