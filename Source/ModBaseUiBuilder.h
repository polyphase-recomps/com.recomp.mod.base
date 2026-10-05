/**
 * @file ModBaseUiBuilder.h
 * @brief Non-destructive UI building for editor tools (header only, editor builds).
 *
 * Builds a UI by node names: a child that already exists is reused exactly as the user
 * left it (moved, restyled, re-bound); only missing children are created with defaults.
 * So running a "Create ... UI" or "Generate ... Scene" tool again only adds what's new.
 *
 *   RecompUi::Builder b;
 *   Canvas* root = RecompUi::RootCanvas(b, "MyUI");
 *   Quad* panel = b.Ensure<Quad>(root, "Panel", [](Quad* q) { RecompUi::Place(q, 16, 16, 300, 200); });
 *   RecompUi::Label(b, panel, "Title", "Hello", 12, 8, 280);
 *   ...
 *   RecompUi::LinkNavigation({{buttonA, buttonB}, {buttonC}});
 */
#pragma once

#include "ModBaseWidgets.h"

#include "Engine.h"
#include "Log.h"
#include "World.h"
#include "Nodes/Widgets/Button.h"
#include "Nodes/Widgets/Canvas.h"
#include "Nodes/Widgets/Quad.h"
#include "Nodes/Widgets/Text.h"
#include "Nodes/Widgets/Widget.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace RecompUi
{
const glm::vec4 kPanelColor = {0.04f, 0.05f, 0.08f, 0.88f};
const glm::vec4 kTextColor = {1.0f, 1.0f, 1.0f, 1.0f};
const glm::vec4 kDimColor = {0.65f, 0.75f, 0.9f, 1.0f};
const glm::vec4 kHeaderColor = {1.0f, 0.85f, 0.35f, 1.0f};
const float kFontSize = 16.0f;
const float kRow = 24.0f;

struct Builder
{
    int added = 0;
    int kept = 0;

    // The child `name` of `parent`, created with `init` if missing. nullptr when a node of
    // that name exists with another type (the user replaced it): it's left alone.
    template <class T>
    T* Ensure(Node* parent, const std::string& name, const std::function<void(T*)>& init)
    {
        if (parent == nullptr)
        {
            return nullptr;
        }
        if (Node* existing = parent->FindChild(name, false))
        {
            ++kept;
            return existing->As<T>();
        }
        T* node = parent->CreateChild<T>(name.c_str());
        init(node);
        ++added;
        return node;
    }
};

inline void Place(Widget* w, float x, float y, float width, float height)
{
    w->SetAnchorMode(AnchorMode::TopLeft);
    w->SetPosition(x, y);
    w->SetDimensions(width, height);
}

inline void Style(Text* t, float size, glm::vec4 color)
{
    t->SetTextSize(size);
    t->SetColor(color);
    t->SetHorizontalJustification(Justification::Left);
    t->SetVerticalJustification(Justification::Center);
}

// A see-through grouping container (alpha stays 1 so children still draw).
inline Widget* Group(Builder& b, Node* parent, const std::string& name, float x, float y, float w, float h)
{
    return b.Ensure<Widget>(parent, name, [&](Widget* g) { Place(g, x, y, w, h); });
}

inline Text* Label(Builder& b, Node* parent, const std::string& name, const std::string& text, float x, float y,
                   float w, float size = kFontSize, glm::vec4 color = kTextColor)
{
    return b.Ensure<Text>(parent, name, [&](Text* t) {
        Place(t, x, y, w, kRow);
        Style(t, size, color);
        t->SetText(text);
    });
}

inline RecompText* Bound(Builder& b, Node* parent, const std::string& name, const std::string& format, float x,
                         float y, float w, float size = kFontSize, glm::vec4 color = kTextColor, bool hideIfMissing = false)
{
    return b.Ensure<RecompText>(parent, name, [&](RecompText* t) {
        Place(t, x, y, w, kRow);
        Style(t, size, color);
        t->SetFormat(format);
        t->SetHideIfMissing(hideIfMissing);
    });
}

inline RecompButton* SettingButton(Builder& b, Node* parent, const std::string& name, const std::string& labelFormat,
                                   const std::string& setting, int direction, float x, float y, float w, float h)
{
    return b.Ensure<RecompButton>(parent, name, [&](RecompButton* btn) {
        Place(btn, x, y, w, h);
        btn->SetTextString(labelFormat);
        btn->SetLabelFormat(labelFormat);
        btn->SetSetting(setting, direction);
    });
}

inline RecompBar* Bar(Builder& b, Node* parent, const std::string& name, const std::string& variable,
                      const std::string& maxVariable, float x, float y, float w, glm::vec4 fill)
{
    return b.Ensure<RecompBar>(parent, name, [&](RecompBar* bar) {
        Place(bar, x, y + 3.0f, w, kRow - 6.0f);
        bar->SetShowPercentage(false);
        bar->SetFillColor(fill);
        bar->SetBackgroundColor({0.15f, 0.15f, 0.18f, 1.0f});
        bar->SetVariables(variable, maxVariable);
    });
}

// Gamepad navigation over rows of buttons: up / down to the same column of the next row
// (or its last button), left / right within a row. Only empty links are set, so links the
// user changed stay. nullptr buttons are skipped.
inline void LinkNavigation(std::vector<std::vector<Button*>> rows)
{
    for (auto& row : rows)
    {
        row.erase(std::remove(row.begin(), row.end(), nullptr), row.end());
    }
    rows.erase(std::remove_if(rows.begin(), rows.end(), [](const std::vector<Button*>& r) { return r.empty(); }),
               rows.end());
    for (size_t r = 0; r < rows.size(); ++r)
    {
        for (size_t c = 0; c < rows[r].size(); ++c)
        {
            Button* btn = rows[r][c];
            if (c > 0 && btn->GetNavLeft() == nullptr) btn->SetNavLeft(rows[r][c - 1]);
            if (c + 1 < rows[r].size() && btn->GetNavRight() == nullptr) btn->SetNavRight(rows[r][c + 1]);
            if (r > 0 && btn->GetNavUp() == nullptr)
            {
                const std::vector<Button*>& up = rows[r - 1];
                btn->SetNavUp(up[c < up.size() ? c : up.size() - 1]);
            }
            if (r + 1 < rows.size() && btn->GetNavDown() == nullptr)
            {
                const std::vector<Button*>& down = rows[r + 1];
                btn->SetNavDown(down[c < down.size() ? c : down.size() - 1]);
            }
        }
    }
}

// The UI's root Canvas in the open scene: an existing one of that name is updated;
// otherwise a new one becomes the scene root (empty scene) or goes under the root.
inline Canvas* RootCanvas(Builder& b, const std::string& name)
{
    World* world = GetWorld(0);
    if (world == nullptr)
    {
        return nullptr;
    }
    Node* root = world->GetRootNode();
    if (root != nullptr)
    {
        Node* existing = (root->GetName() == name) ? root : root->FindChild(name, true);
        if (existing != nullptr)
        {
            ++b.kept;
            Canvas* canvas = existing->As<Canvas>();
            if (canvas == nullptr)
            {
                LogWarning("%s exists but is not a Canvas: left as it is", name.c_str());
            }
            return canvas;
        }
        Canvas* canvas = root->CreateChild<Canvas>(name.c_str());
        canvas->SetFullScreen();
        ++b.added;
        return canvas;
    }
    SharedPtr<Canvas> canvas = Node::Construct<Canvas>();
    canvas->SetName(name);
    canvas->SetFullScreen();
    world->SetRootNode(canvas.Get());
    ++b.added;
    return canvas.Get();
}

inline void Report(const Builder& b, const std::string& what)
{
    if (b.kept == 0)
    {
        LogDebug("%s created (%d nodes).", what.c_str(), b.added);
    }
    else
    {
        LogDebug("%s updated: %d node(s) added, %d existing left untouched.", what.c_str(), b.added, b.kept);
    }
}
}
