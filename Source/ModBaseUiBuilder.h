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
 *
 * Responsive layouts: Array (ArrayWidget column/row), Scroll (ScrollContainer), and the
 * Placer helpers Filled / FullWidth / FillRest instead of fixed spots. Never pixel
 * margins on stretched widgets in a saved scene: only ratios survive the save.
 */
#pragma once

#include "ModBaseWidgets.h"

#include "Engine.h"
#include "Log.h"
#include "World.h"
#include "Nodes/Widgets/Button.h"
#include "Nodes/Widgets/Canvas.h"
#include "Nodes/Widgets/Quad.h"
#include "Nodes/Widgets/ScrollContainer.h"
#include "Nodes/Widgets/Text.h"
#include "Nodes/Widgets/Widget.h"
#include "Property.h"

#include <algorithm>
#include <cstring>
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

// How a widget sits in its parent: `Placer` lets the helpers below be used inside
// responsive layouts (stretch, right-aligned, array rows) as well as at fixed spots.
using Placer = std::function<void(Widget*)>;

inline void Place(Widget* w, float x, float y, float width, float height)
{
    w->SetAnchorMode(AnchorMode::TopLeft);
    w->SetPosition(x, y);
    w->SetDimensions(width, height);
}

inline Placer At(float x, float y, float width, float height)
{
    return [=](Widget* w) { Place(w, x, y, width, height); };
}

// Fills the parent. Stretch modes are saved as 0-1 ratios of the parent (Offset 0,0 /
// Size 1,1): pixel margins (Widget::SetMargins) aren't saved with a scene, so after a
// reload they'd read as ratios. Inset with an ArrayWidget's padding instead.
inline void Full(Widget* w)
{
    w->SetAnchorMode(AnchorMode::FullStretch);
    w->SetRatios(0.0f, 0.0f, 1.0f, 1.0f);
}

inline Placer Filled()
{
    return [](Widget* w) { Full(w); };
}

// Full width of the parent, fixed height (y pixels down). In an ArrayWidget row it takes
// the width the fixed-size siblings leave; in a column it is one full-width line.
inline Placer FullWidth(float height, float y = 0.0f)
{
    return [=](Widget* w) {
        w->SetAnchorMode(AnchorMode::FillHorizontal);
        w->SetHeight(height);
        w->SetY(y);
    };
}

// Takes what's left in an ArrayWidget (both axes).
inline void FillRest(Widget* w)
{
    w->SetAnchorMode(AnchorMode::Fill);
}

// Sets a property by its editor name: reaches engine widgets an addon can't call
// directly (ArrayWidget isn't exported from the engine).
inline bool SetProperty(Node* node, const char* name, const std::function<void(Property&)>& set)
{
    std::vector<Property> props;
    node->GatherProperties(props);
    for (Property& p : props)
    {
        if (p.mName == name)
        {
            set(p);
            return true;
        }
    }
    return false;
}

// The child `name` made from an engine type name. nullptr when a node of that name has
// another type (left alone) or the engine has no such type.
inline Widget* EnsureTyped(Builder& b, Node* parent, const std::string& name, const char* typeName,
                           const std::function<void(Widget*)>& init)
{
    if (parent == nullptr)
    {
        return nullptr;
    }
    if (Node* existing = parent->FindChild(name, false))
    {
        ++b.kept;
        return std::strcmp(existing->RuntimeName(), typeName) == 0 ? existing->As<Widget>() : nullptr;
    }
    Node* node = parent->CreateChild(typeName);
    Widget* w = node ? node->As<Widget>() : nullptr;
    if (w == nullptr)
    {
        if (node != nullptr) parent->RemoveChild(node);
        LogWarning("Recomp UI: the engine has no %s widget", typeName);
        return nullptr;
    }
    node->SetName(name);
    init(w);
    ++b.added;
    return w;
}

// An ArrayWidget: lays its children out in a column (or a row) with spacing/padding.
// Its own size isn't set by its content: give a scrolled list its total height.
inline Widget* Array(Builder& b, Node* parent, const std::string& name, bool horizontal, float spacing, float padding,
                     const Placer& place, bool center = false)
{
    return EnsureTyped(b, parent, name, "ArrayWidget", [&](Widget* w) {
        place(w);
        SetProperty(w, "Orientation", [&](Property& p) { p.SetByte(horizontal ? 1 : 0); });
        SetProperty(w, "Spacing", [&](Property& p) { p.SetFloat(spacing); });
        SetProperty(w, "Center", [&](Property& p) { p.SetBool(center); });
        for (const char* side : {"Padding Left", "Padding Top", "Padding Right", "Padding Bottom"})
        {
            SetProperty(w, side, [&](Property& p) { p.SetFloat(padding); });
        }
    });
}

// A ScrollContainer: its first child is the scrolled content. Vertical (content as wide
// as the view, scrolls up/down) or horizontal (as tall as the view, scrolls sideways).
// The gamepad scrolls it through RecompMenuController (selected button kept in view).
inline ScrollContainer* Scroll(Builder& b, Node* parent, const std::string& name, bool horizontal, const Placer& place)
{
    return b.Ensure<ScrollContainer>(parent, name, [&](ScrollContainer* s) {
        place(s);
        s->SetScrollSizeMode(horizontal ? ScrollSizeMode::FitHeight : ScrollSizeMode::FitWidth);
        s->SetHorizontalScrollbarMode(ScrollbarMode::Hidden);
        s->SetVerticalScrollbarMode(horizontal ? ScrollbarMode::Hidden : ScrollbarMode::Auto);
        s->SetScrollbarWidth(6.0f);
        s->SetChildInputPriority(true);
    });
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

inline Text* Label(Builder& b, Node* parent, const std::string& name, const std::string& text, const Placer& place,
                   float size = kFontSize, glm::vec4 color = kTextColor)
{
    return b.Ensure<Text>(parent, name, [&](Text* t) {
        place(t);
        Style(t, size, color);
        t->SetText(text);
    });
}

inline Text* Label(Builder& b, Node* parent, const std::string& name, const std::string& text, float x, float y,
                   float w, float size = kFontSize, glm::vec4 color = kTextColor)
{
    return Label(b, parent, name, text, At(x, y, w, kRow), size, color);
}

inline RecompText* Bound(Builder& b, Node* parent, const std::string& name, const std::string& format,
                         const Placer& place, float size = kFontSize, glm::vec4 color = kTextColor,
                         bool hideIfMissing = false)
{
    return b.Ensure<RecompText>(parent, name, [&](RecompText* t) {
        place(t);
        Style(t, size, color);
        t->SetFormat(format);
        t->SetHideIfMissing(hideIfMissing);
    });
}

inline RecompText* Bound(Builder& b, Node* parent, const std::string& name, const std::string& format, float x,
                         float y, float w, float size = kFontSize, glm::vec4 color = kTextColor, bool hideIfMissing = false)
{
    return Bound(b, parent, name, format, At(x, y, w, kRow), size, color, hideIfMissing);
}

inline RecompButton* SettingButton(Builder& b, Node* parent, const std::string& name, const std::string& labelFormat,
                                   const std::string& setting, int direction, const Placer& place)
{
    return b.Ensure<RecompButton>(parent, name, [&](RecompButton* btn) {
        place(btn);
        btn->SetTextString(labelFormat);
        btn->SetLabelFormat(labelFormat);
        btn->SetSetting(setting, direction);
    });
}

inline RecompButton* SettingButton(Builder& b, Node* parent, const std::string& name, const std::string& labelFormat,
                                   const std::string& setting, int direction, float x, float y, float w, float h)
{
    return SettingButton(b, parent, name, labelFormat, setting, direction, At(x, y, w, h));
}

inline RecompBar* Bar(Builder& b, Node* parent, const std::string& name, const std::string& variable,
                      const std::string& maxVariable, const Placer& place, glm::vec4 fill)
{
    return b.Ensure<RecompBar>(parent, name, [&](RecompBar* bar) {
        place(bar);
        bar->SetShowPercentage(false);
        bar->SetFillColor(fill);
        bar->SetBackgroundColor({0.15f, 0.15f, 0.18f, 1.0f});
        bar->SetVariables(variable, maxVariable);
    });
}

inline RecompBar* Bar(Builder& b, Node* parent, const std::string& name, const std::string& variable,
                      const std::string& maxVariable, float x, float y, float w, glm::vec4 fill)
{
    return Bar(b, parent, name, variable, maxVariable, At(x, y + 3.0f, w, kRow - 6.0f), fill);
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
