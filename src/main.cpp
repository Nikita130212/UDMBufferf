#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <cmath>
#include <unordered_map>

using namespace geode::prelude;

// ===========================================================================
// Перечисления
// ===========================================================================
enum class Mode : int {
    Cube = 0, Ship = 1, Ball = 2, UFO = 3,
    Wave = 4, Robot = 5, Spider = 6, Swing = 7, Unknown = -1
};

enum class ClickType {
    Landing, Orb, MinHold, MinGap, Grid, Unknown
};

// ===========================================================================
// Утилиты (логика буфера)
// ===========================================================================
namespace {
    inline Mode getMode(PlayerObject* p) {
        if (!p) return Mode::Unknown;
        if (p->m_isShip)   return Mode::Ship;
        if (p->m_isBall)   return Mode::Ball;
        if (p->m_isBird)   return Mode::UFO;
        if (p->m_isDart)   return Mode::Wave;
        if (p->m_isRobot)  return Mode::Robot;
        if (p->m_isSpider) return Mode::Spider;
        if (p->m_isSwing)  return Mode::Swing;
        return Mode::Cube;
    }

    inline bool masterEnabled() {
        return Mod::get()->getSettingValue<bool>("master-enable");
    }

    inline void dbg(const std::string& msg) {
        if (Mod::get()->getSettingValue<bool>("log"))
            log::info("[UDMURT] {}", msg);
    }

    inline bool isModeEnabled(Mode m) {
        auto mod = Mod::get();
        switch (m) {
            case Mode::Cube:   return mod->getSettingValue<bool>("mode-cube");
            case Mode::Ship:   return mod->getSettingValue<bool>("mode-ship");
            case Mode::Ball:   return mod->getSettingValue<bool>("mode-ball");
            case Mode::UFO:    return mod->getSettingValue<bool>("mode-ufo");
            case Mode::Wave:   return mod->getSettingValue<bool>("mode-wave");
            case Mode::Robot:  return mod->getSettingValue<bool>("mode-robot");
            case Mode::Spider: return mod->getSettingValue<bool>("mode-spider");
            case Mode::Swing:  return mod->getSettingValue<bool>("mode-swing");
            default:           return true;
        }
    }

    inline bool isClickTypeEnabled(ClickType t) {
        auto mod = Mod::get();
        switch (t) {
            case ClickType::Landing: return mod->getSettingValue<bool>("click-landing");
            case ClickType::Orb:     return mod->getSettingValue<bool>("click-orb");
            case ClickType::MinHold: return mod->getSettingValue<bool>("click-min-hold");
            case ClickType::MinGap:  return mod->getSettingValue<bool>("click-min-gap");
            case ClickType::Grid:    return mod->getSettingValue<bool>("click-grid");
            default:                 return true;
        }
    }

    ClickType inferClickType(PlayerObject* p) {
        if (!p) return ClickType::Unknown;
        if (p->m_touchingRings && p->m_touchingRings->count() > 0)
            return ClickType::Orb;
        auto m = getMode(p);
        if (m == Mode::Wave || m == Mode::Ship || m == Mode::UFO || m == Mode::Swing) {
            if (auto bufMod = Loader::get()->getLoadedMod("lyophi.buffermod")) {
                int style = static_cast<int>(bufMod->getSettingValue<int64_t>("wave-mode"));
                if (style == 0) return ClickType::MinHold;
                if (style == 1) return ClickType::MinGap;
                if (style == 2) return ClickType::Grid;
            }
            return ClickType::MinHold;
        }
        return ClickType::Landing;
    }

    inline int getWindowFor(ClickType t) {
        auto mod = Mod::get();
        switch (t) {
            case ClickType::Landing: return static_cast<int>(mod->getSettingValue<int64_t>("landing-window"));
            case ClickType::Orb:     return static_cast<int>(mod->getSettingValue<int64_t>("orb-window"));
            case ClickType::MinHold: return static_cast<int>(mod->getSettingValue<int64_t>("min-hold-window"));
            case ClickType::MinGap:  return static_cast<int>(mod->getSettingValue<int64_t>("min-gap-window"));
            case ClickType::Grid:    return static_cast<int>(mod->getSettingValue<int64_t>("grid-window"));
            default:                 return 0;
        }
    }

    inline int getModeOverride(Mode m) {
        auto mod = Mod::get();
        switch (m) {
            case Mode::Ship:  return static_cast<int>(mod->getSettingValue<int64_t>("ship-window"));
            case Mode::UFO:   return static_cast<int>(mod->getSettingValue<int64_t>("ufo-window"));
            case Mode::Wave:  return static_cast<int>(mod->getSettingValue<int64_t>("wave-window"));
            case Mode::Swing: return static_cast<int>(mod->getSettingValue<int64_t>("swing-window"));
            default:          return 0;
        }
    }

    inline float toSeconds(int value) {
        auto mod = Mod::get();
        bool frames = mod->getSettingValue<std::string>("unit") == "frames";
        if (frames) {
            int fps = static_cast<int>(mod->getSettingValue<int64_t>("fps"));
            if (fps < 1) fps = 60;
            return static_cast<float>(value) / static_cast<float>(fps);
        }
        return static_cast<float>(value) / 240.0f;
    }

    inline int resolveWindowValue(PlayerObject* p, ClickType t) {
        int ov = getModeOverride(getMode(p));
        if (ov > 0) return ov;
        return getWindowFor(t);
    }

    inline bool shouldSuppress(PlayerObject* p) {
        if (!masterEnabled()) return false;
        if (!isModeEnabled(getMode(p))) return true;
        if (!isClickTypeEnabled(inferClickType(p))) return true;
        return false;
    }

    inline cocos2d::ccColor3B colorBlack()  { return {0, 0, 0}; }
    inline cocos2d::ccColor3B colorRed()    { return {200, 30, 30}; }
    inline cocos2d::ccColor3B colorWhite()  { return {255, 255, 255}; }
}

// ===========================================================================
// Кастомное окно настроек UDMURT (Geode v5: Popup не шаблонный)
// ===========================================================================
class UdmurtSettingsPopup : public geode::Popup {
protected:
    std::unordered_map<int, std::string> m_settingIds;

    bool init() {
        if (!geode::Popup::init(420.f, 360.f))
            return false;

        if (m_bgSprite) {
            m_bgSprite->setColor(colorBlack());
            m_bgSprite->setOpacity(230);
        }
        this->setTitle("UDMURT BUFFERS");

        auto scroll = ScrollLayer::create({380.f, 280.f});
        scroll->setPosition({20.f, 30.f});
        m_mainLayer->addChild(scroll);

        auto content = scroll->m_contentLayer;
        float y = 0.f;
        float lineH = 28.f;

        // --- Секция MODES ---
        auto modeLabel = CCLabelBMFont::create("MODES", "goldFont.fnt");
        modeLabel->setColor(colorRed());
        modeLabel->setScale(0.8f);
        modeLabel->setPosition({190.f, y});
        content->addChild(modeLabel);
        y -= lineH * 1.5f;

        struct Entry { const char* id; const char* name; };
        Entry modes[] = {
            {"mode-cube",   "Cube"},
            {"mode-ship",   "Ship"},
            {"mode-ball",   "Ball"},
            {"mode-ufo",    "UFO"},
            {"mode-wave",   "Wave"},
            {"mode-robot",  "Robot"},
            {"mode-spider", "Spider"},
            {"mode-swing",  "Swing"},
        };
        for (auto& e : modes) {
            this->addToggle(content, e.id, e.name, y);
            y -= lineH;
        }

        // --- Секция CLICK TYPES ---
        y -= lineH * 0.5f;
        auto clickLabel = CCLabelBMFont::create("CLICK TYPES", "goldFont.fnt");
        clickLabel->setColor(colorRed());
        clickLabel->setScale(0.8f);
        clickLabel->setPosition({190.f, y});
        content->addChild(clickLabel);
        y -= lineH * 1.5f;

        Entry clicks[] = {
            {"click-landing", "Landing Buffer"},
            {"click-orb",     "Orb Buffer"},
            {"click-min-hold","Min Hold"},
            {"click-min-gap", "Min Gap"},
            {"click-grid",    "Grid"},
        };
        for (auto& e : clicks) {
            this->addToggle(content, e.id, e.name, y);
            y -= lineH;
        }

        // --- MASTER ---
        y -= lineH * 0.5f;
        this->addToggle(content, "master-enable", "MASTER ENABLE", y);
        y -= lineH;

        content->setContentSize({380.f, std::abs(y) + 20.f});
        content->setPositionY(scroll->getContentSize().height - content->getContentSize().height);
        scroll->moveToTop();

        return true;
    }

    void addToggle(CCNode* parent, const char* settingId, const char* labelText, float y) {
        auto lbl = CCLabelBMFont::create(labelText, "bigFont.fnt");
        lbl->setColor(colorWhite());
        lbl->setScale(0.55f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({10.f, y});
        parent->addChild(lbl);

        auto stripe = CCLayerColor::create({200, 30, 30, 80});
        stripe->setContentSize({360.f, 2.f});
        stripe->setPosition({10.f, y - 12.f});
        parent->addChild(stripe);

        auto offSpr = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
        offSpr->setColor(colorBlack());
        auto onSpr  = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
        onSpr->setColor(colorRed());

        auto toggle = CCMenuItemToggler::create(
            offSpr, onSpr, this,
            menu_selector(UdmurtSettingsPopup::onToggle)
        );

        int tag = this->hashSettingId(settingId);
        m_settingIds[tag] = settingId;
        toggle->setTag(tag);
        toggle->setPosition({360.f, y});
        toggle->toggle(Mod::get()->getSettingValue<bool>(settingId));

        auto menu = CCMenu::create();
        menu->addChild(toggle);
        menu->setPosition({0.f, 0.f});
        parent->addChild(menu);
    }

    int hashSettingId(const char* id) {
        int h = 0;
        for (const char* p = id; *p; ++p) h = h * 31 + *p;
        return h;
    }

    void onToggle(CCObject* sender) {
        auto toggle = static_cast<CCMenuItemToggler*>(sender);
        bool newVal = !toggle->isToggled();
        auto it = m_settingIds.find(toggle->getTag());
        if (it != m_settingIds.end()) {
            Mod::get()->setSettingValue<bool>(it->second, newVal);
            dbg(fmt::format("Toggle {} -> {}", it->second, newVal));
        }
    }

public:
    static UdmurtSettingsPopup* create() {
        auto ret = new UdmurtSettingsPopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// ===========================================================================
// Хуки PlayerObject и PlayLayer
// ===========================================================================
class $modify(UDMURTPlayer, PlayerObject) {
    struct Fields {
        float m_bufferWindow = 0.0f;
        float m_bufferElapsed = 0.0f;
    };

    void clearBufferState() {
        m_fields->m_bufferWindow = 0.0f;
        m_fields->m_bufferElapsed = 0.0f;
        this->m_jumpBuffered = false;
        this->m_holdingButtons.clear();
    }

    bool pushButton(PlayerButton button) {
        if (shouldSuppress(this)) {
            clearBufferState();
            dbg("pushButton suppressed");
        } else {
            auto t = inferClickType(this);
            int val = resolveWindowValue(this, t);
            if (val > 0) {
                m_fields->m_bufferWindow = toSeconds(val);
                m_fields->m_bufferElapsed = 0.0f;
                dbg(fmt::format("pushButton window={} -> {}s", val, m_fields->m_bufferWindow));
            }
        }
        return PlayerObject::pushButton(button);
    }

    bool releaseButton(PlayerButton button) {
        if (shouldSuppress(this)) {
            clearBufferState();
            dbg("releaseButton suppressed");
        }
        return PlayerObject::releaseButton(button);
    }

    void update(float dt) {
        PlayerObject::update(dt);
        if (!masterEnabled()) return;

        auto m = getMode(this);
        if (!isModeEnabled(m)) {
            clearBufferState();
            return;
        }

        auto t = inferClickType(this);
        if (!isClickTypeEnabled(t)) {
            clearBufferState();
            return;
        }

        int val = resolveWindowValue(this, t);
        if (val > 0) {
            m_fields->m_bufferWindow = toSeconds(val);
            if (this->m_jumpBuffered || !this->m_holdingButtons.empty()) {
                m_fields->m_bufferElapsed += dt;
                if (m_fields->m_bufferElapsed > m_fields->m_bufferWindow) {
                    this->m_jumpBuffered = false;
                    this->m_holdingButtons.clear();
                }
            } else {
                m_fields->m_bufferElapsed = 0.0f;
            }
        }
    }
};

class $modify(UDMURTLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        if (!masterEnabled()) return;

        for (auto* p : {this->m_player1, this->m_player2}) {
            if (!p) continue;
            if (shouldSuppress(p)) {
                static_cast<UDMURTPlayer*>(p)->clearBufferState();
            }
        }
    }
};

// ===========================================================================
// Открытие UI через Button Setting (Geode v5)
// ===========================================================================
$execute {
    ButtonSettingPressedEventV3(Mod::get(), "open-ui").listen([](auto buttonKey) {
        if (buttonKey == "open") {
            UdmurtSettingsPopup::create()->show();
        }
    }).leak();

    log::info("UDMURT Buffers loaded (Geode v5.7.1).");
}
