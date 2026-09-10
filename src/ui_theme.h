#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

namespace UITheme {

    // Modern Dark Palette Matching Dashboard Mockup
    namespace Colors {
        const Gdiplus::Color BgCanvas(255, 14, 16, 23);           // #0E1017 deep background
        const Gdiplus::Color BgHeader(255, 17, 19, 28);           // #11131C header
        const Gdiplus::Color BgSidebar(255, 17, 19, 28);          // #11131C sidebar
        const Gdiplus::Color BgSidebarHover(255, 25, 29, 43);     // #191D2B
        const Gdiplus::Color BgSidebarActive(255, 30, 35, 52);    // #1E2334
        const Gdiplus::Color BgCard(255, 22, 25, 38);             // #161926 slate card
        const Gdiplus::Color BgCardHover(255, 28, 32, 48);        // #1C2030
        const Gdiplus::Color BgInput(255, 15, 17, 27);            // #0F111B recessed input
        const Gdiplus::Color BorderDark(255, 35, 39, 60);         // #23273C card & divider border
        const Gdiplus::Color BorderInput(255, 39, 44, 64);        // #272C40
        const Gdiplus::Color BorderFocus(255, 99, 102, 241);      // Indigo #6366F1
        
        const Gdiplus::Color TextPrimary(255, 241, 245, 249);     // #F1F5F9
        const Gdiplus::Color TextSecondary(255, 142, 154, 176);   // #8E9AB0
        const Gdiplus::Color TextMuted(255, 100, 116, 139);       // #64748B
        
        const Gdiplus::Color Primary(255, 79, 70, 229);           // Indigo #4F46E5
        const Gdiplus::Color PrimaryHover(255, 99, 102, 241);     // #6366F1
        const Gdiplus::Color PrimaryText(255, 255, 255, 255);
        
        const Gdiplus::Color ActiveGreen(255, 16, 185, 129);      // Emerald #10B981
        const Gdiplus::Color ActiveGreenBg(255, 10, 43, 32);      // #0A2B20
        const Gdiplus::Color BadgeIpBg(255, 28, 32, 50);          // #1C2032 IP pill
        
        const Gdiplus::Color DisabledGray(255, 100, 116, 139);    // #64748B
        const Gdiplus::Color DisabledGrayBg(255, 25, 30, 44);     // #191E2C
        
        const Gdiplus::Color Danger(255, 239, 68, 68);            // Rose #EF4444
        const Gdiplus::Color DangerHover(255, 220, 38, 38);
        const Gdiplus::Color DangerBg(255, 60, 20, 28);
        
        const Gdiplus::Color AccentCyan(255, 56, 189, 248);       // #38BDF8
        const Gdiplus::Color AccentAmber(255, 245, 158, 11);      // #F59E0B
        const Gdiplus::Color AccentPurple(255, 168, 85, 247);     // #A855F7
        
        const Gdiplus::Color ScrollTrack(255, 17, 19, 28);
        const Gdiplus::Color ScrollThumb(255, 45, 52, 75);
        const Gdiplus::Color ScrollThumbHover(255, 65, 75, 105);
    }

    // Helper to create rounded rectangle path
    inline void AddRoundedRectangle(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
        float diameter = radius * 2.0f;
        if (diameter > rect.Width) diameter = rect.Width;
        if (diameter > rect.Height) diameter = rect.Height;

        Gdiplus::RectF arc(rect.X, rect.Y, diameter, diameter);
        path.AddArc(arc, 180.0f, 90.0f); // Top-left

        arc.X = (rect.X + rect.Width) - diameter;
        path.AddArc(arc, 270.0f, 90.0f); // Top-right

        arc.Y = (rect.Y + rect.Height) - diameter;
        path.AddArc(arc, 0.0f, 90.0f);   // Bottom-right

        arc.X = rect.X;
        path.AddArc(arc, 90.0f, 90.0f);  // Bottom-left
        path.CloseFigure();
    }

    inline void DrawRoundedRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, float radius, const Gdiplus::Color& fillColor, const Gdiplus::Color& borderColor, float borderWidth = 1.0f) {
        Gdiplus::GraphicsPath path;
        AddRoundedRectangle(path, rect, radius);

        if (fillColor.GetAlpha() > 0) {
            Gdiplus::SolidBrush fillBrush(fillColor);
            g.FillPath(&fillBrush, &path);
        }

        if (borderWidth > 0.0f && borderColor.GetAlpha() > 0) {
            Gdiplus::Pen pen(borderColor, borderWidth);
            g.DrawPath(&pen, &path);
        }
    }

    // Draw a modern iOS/Fluent style toggle switch
    inline void DrawToggleSwitch(Gdiplus::Graphics& g, float x, float y, float width, float height, bool isOn, bool isHovered) {
        float radius = height / 2.0f;
        Gdiplus::RectF trackRect(x, y, width, height);

        Gdiplus::Color trackColor;
        if (isOn) {
            trackColor = isHovered ? Gdiplus::Color(255, 16, 185, 129) : Gdiplus::Color(255, 14, 165, 115);
        } else {
            trackColor = isHovered ? Gdiplus::Color(255, 55, 65, 88) : Gdiplus::Color(255, 40, 48, 68);
        }

        DrawRoundedRect(g, trackRect, radius, trackColor, Gdiplus::Color(0, 0, 0, 0), 0);

        // Thumb circle
        float padding = 2.5f;
        float thumbDiameter = height - (padding * 2.0f);
        float thumbX = isOn ? (x + width - padding - thumbDiameter) : (x + padding);
        float thumbY = y + padding;

        Gdiplus::SolidBrush thumbBrush(Gdiplus::Color(255, 255, 255, 255));
        g.FillEllipse(&thumbBrush, thumbX, thumbY, thumbDiameter, thumbDiameter);
    }

    // Draw pill badge
    inline void DrawBadge(Gdiplus::Graphics& g, const std::wstring& text, float x, float y, const Gdiplus::Color& textColor, const Gdiplus::Color& bgColor, Gdiplus::Font* font, float paddingH = 8.0f, float paddingV = 3.0f, const Gdiplus::Color& borderColor = Gdiplus::Color(0, 0, 0, 0)) {
        Gdiplus::RectF layoutRect(0, 0, 1000, 100);
        Gdiplus::RectF boundRect;
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        g.MeasureString(text.c_str(), -1, font, layoutRect, &sf, &boundRect);

        float badgeW = boundRect.Width + paddingH * 2.0f;
        float badgeH = boundRect.Height + paddingV * 2.0f;
        Gdiplus::RectF badgeRect(x, y, badgeW, badgeH);

        DrawRoundedRect(g, badgeRect, badgeH / 2.0f, bgColor, borderColor, borderColor.GetAlpha() > 0 ? 1.0f : 0.0f);

        Gdiplus::SolidBrush textBrush(textColor);
        g.DrawString(text.c_str(), -1, font, badgeRect, &sf, &textBrush);
    }

    // -------------------------------------------------------------
    // Vector Icon Renderers (100% Native GDI+, Zero Dependencies)
    // -------------------------------------------------------------

    // Draw Stylized Server Stack Logo (Mockup Top-Left)
    inline void DrawServerStackLogo(Gdiplus::Graphics& g, float x, float y, float size = 32.0f) {
        float w = size;
        float h = size;

        // 3 Server Units
        float bladeH = h * 0.20f;
        float bladeSpacing = h * 0.06f;

        Gdiplus::Color cTop(255, 129, 140, 248);     // Indigo
        Gdiplus::Color cMid(255, 99, 102, 241);      // Purple
        Gdiplus::Color cBot(255, 67, 56, 202);       // Deep Purple
        Gdiplus::Color cDot(255, 56, 189, 248);      // Cyan LED

        for (int i = 0; i < 3; ++i) {
            float by = y + (float)i * (bladeH + bladeSpacing);
            Gdiplus::RectF bRect(x + 2.0f, by, w - 4.0f, bladeH);
            Gdiplus::Color bColor = (i == 0) ? cTop : (i == 1 ? cMid : cBot);
            DrawRoundedRect(g, bRect, 3.0f, bColor, Gdiplus::Color(0, 0, 0, 0), 0);

            // LED dots
            Gdiplus::SolidBrush dotBrush(cDot);
            g.FillEllipse(&dotBrush, x + 6.0f, by + bladeH * 0.35f, 2.5f, 2.5f);
            g.FillEllipse(&dotBrush, x + 11.0f, by + bladeH * 0.35f, 2.5f, 2.5f);

            // Subtle horizontal line on blade
            Gdiplus::Pen linePen(Gdiplus::Color(120, 255, 255, 255), 1.0f);
            g.DrawLine(&linePen, x + 16.0f, by + bladeH * 0.5f, x + w - 7.0f, by + bladeH * 0.5f);
        }

        // Connection stand / cable base
        float baseY = y + 3.0f * (bladeH + bladeSpacing) + 1.0f;
        Gdiplus::Pen standPen(Gdiplus::Color(255, 99, 102, 241), 1.5f);
        g.DrawLine(&standPen, x + w * 0.5f, baseY - 2.0f, x + w * 0.5f, baseY + 3.0f);
        g.DrawLine(&standPen, x + 7.0f, baseY + 3.0f, x + w - 7.0f, baseY + 3.0f);
    }

    // Windows 4-Pane Blue Logo
    inline void DrawWindowsLogo(Gdiplus::Graphics& g, float x, float y, float size = 13.0f) {
        float half = (size - 2.0f) / 2.0f;
        Gdiplus::SolidBrush b(Gdiplus::Color(255, 0, 164, 239)); // Microsoft Blue
        g.FillRectangle(&b, x, y, half, half);
        g.FillRectangle(&b, x + half + 2.0f, y, half, half);
        g.FillRectangle(&b, x, y + half + 2.0f, half, half);
        g.FillRectangle(&b, x + half + 2.0f, y + half + 2.0f, half, half);
    }

    // 4-Square Grid (Dashboard / All Entries)
    inline void DrawGridIcon(Gdiplus::Graphics& g, float cx, float cy, float size, const Gdiplus::Color& color) {
        float h = size * 0.42f;
        float gap = size * 0.16f;
        float startX = cx - size * 0.5f;
        float startY = cy - size * 0.5f;

        Gdiplus::SolidBrush b(color);
        Gdiplus::RectF r1(startX, startY, h, h);
        Gdiplus::RectF r2(startX + h + gap, startY, h, h);
        Gdiplus::RectF r3(startX, startY + h + gap, h, h);
        Gdiplus::RectF r4(startX + h + gap, startY + h + gap, h, h);

        DrawRoundedRect(g, r1, 1.5f, color, Gdiplus::Color(0,0,0,0), 0);
        DrawRoundedRect(g, r2, 1.5f, color, Gdiplus::Color(0,0,0,0), 0);
        DrawRoundedRect(g, r3, 1.5f, color, Gdiplus::Color(0,0,0,0), 0);
        DrawRoundedRect(g, r4, 1.5f, color, Gdiplus::Color(0,0,0,0), 0);
    }

    // Lightning Bolt (Active)
    inline void DrawLightningIcon(Gdiplus::Graphics& g, float cx, float cy, float size, const Gdiplus::Color& color) {
        Gdiplus::PointF pts[6] = {
            Gdiplus::PointF(cx + size * 0.05f, cy - size * 0.5f),
            Gdiplus::PointF(cx - size * 0.35f, cy + size * 0.05f),
            Gdiplus::PointF(cx - size * 0.05f, cy + size * 0.05f),
            Gdiplus::PointF(cx - size * 0.15f, cy + size * 0.5f),
            Gdiplus::PointF(cx + size * 0.40f, cy - size * 0.05f),
            Gdiplus::PointF(cx + size * 0.05f, cy - size * 0.05f)
        };
        Gdiplus::SolidBrush b(color);
        g.FillPolygon(&b, pts, 6);
    }

    // Circle with Horizontal Minus (Disabled)
    inline void DrawCircleMinusIcon(Gdiplus::Graphics& g, float cx, float cy, float size, const Gdiplus::Color& color) {
        Gdiplus::Pen pen(color, 1.5f);
        float r = size * 0.48f;
        g.DrawEllipse(&pen, cx - r, cy - r, r * 2.0f, r * 2.0f);
        g.DrawLine(&pen, cx - r * 0.6f, cy, cx + r * 0.6f, cy);
    }

    // Users / Groups Icon
    inline void DrawUsersIcon(Gdiplus::Graphics& g, float cx, float cy, float size, const Gdiplus::Color& color) {
        Gdiplus::Pen pen(color, 1.5f);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);

        // Head 1
        float r = size * 0.22f;
        g.DrawEllipse(&pen, cx - r - 2.0f, cy - size * 0.40f, r * 2.0f, r * 2.0f);
        // Body 1 arc
        g.DrawArc(&pen, cx - r * 2.0f - 2.0f, cy - size * 0.05f, (r * 2.0f) * 2.0f, size * 0.7f, 200.0f, 140.0f);

        // Head 2 (behind/right)
        g.DrawEllipse(&pen, cx + 3.0f, cy - size * 0.30f, r * 1.6f, r * 1.6f);
        // Body 2 arc
        g.DrawArc(&pen, cx - 1.0f, cy, (r * 1.6f) * 2.0f, size * 0.6f, 200.0f, 130.0f);
    }

    // Gear / Settings Icon
    inline void DrawGearIcon(Gdiplus::Graphics& g, float cx, float cy, float size, const Gdiplus::Color& color) {
        Gdiplus::Pen pen(color, 1.5f);
        float r = size * 0.36f;
        g.DrawEllipse(&pen, cx - r, cy - r, r * 2.0f, r * 2.0f);

        // 6 small teeth radiating
        for (int i = 0; i < 6; ++i) {
            float angle = (float)i * 60.0f * 3.14159f / 180.0f;
            float x1 = cx + cos(angle) * (r - 1.0f);
            float y1 = cy + sin(angle) * (r - 1.0f);
            float x2 = cx + cos(angle) * (r + 3.0f);
            float y2 = cy + sin(angle) * (r + 3.0f);
            g.DrawLine(&pen, x1, y1, x2, y2);
        }
        // Center hole
        Gdiplus::SolidBrush b(color);
        g.FillEllipse(&b, cx - 2.0f, cy - 2.0f, 4.0f, 4.0f);
    }

    // Document / Logs Icon
    inline void DrawDocumentIcon(Gdiplus::Graphics& g, float cx, float cy, float size, const Gdiplus::Color& color) {
        float w = size * 0.72f;
        float h = size * 0.90f;
        float x = cx - w * 0.5f;
        float y = cy - h * 0.5f;

        Gdiplus::Pen pen(color, 1.4f);
        Gdiplus::RectF docRect(x, y, w, h);
        DrawRoundedRect(g, docRect, 2.0f, Gdiplus::Color(0,0,0,0), color, 1.4f);

        // 3 horizontal text lines
        g.DrawLine(&pen, x + 3.0f, y + h * 0.35f, x + w - 3.0f, y + h * 0.35f);
        g.DrawLine(&pen, x + 3.0f, y + h * 0.55f, x + w - 3.0f, y + h * 0.55f);
        g.DrawLine(&pen, x + 3.0f, y + h * 0.75f, x + w * 0.65f, y + h * 0.75f);
    }

    // Edit Pencil Icon
    inline void DrawPencilIcon(Gdiplus::Graphics& g, float cx, float cy, float size, const Gdiplus::Color& color) {
        Gdiplus::Pen pen(color, 1.5f);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);

        float s = size * 0.45f;
        // Diagonal pencil barrel
        g.DrawLine(&pen, cx - s, cy + s, cx + s, cy - s);
        // Small tip
        g.DrawLine(&pen, cx - s, cy + s, cx - s + 2.5f, cy + s);
        g.DrawLine(&pen, cx - s, cy + s, cx - s, cy + s - 2.5f);
        // Top cap mark
        g.DrawLine(&pen, cx + s - 2.5f, cy - s - 1.0f, cx + s + 1.0f, cy - s + 2.5f);
    }

    // Trash Can Icon
    inline void DrawTrashIcon(Gdiplus::Graphics& g, float cx, float cy, float size, const Gdiplus::Color& color) {
        Gdiplus::Pen pen(color, 1.4f);
        float w = size * 0.65f;
        float h = size * 0.75f;
        float x = cx - w * 0.5f;
        float y = cy - h * 0.5f;

        // Top lid & handle
        g.DrawLine(&pen, x - 2.0f, y + 2.0f, x + w + 2.0f, y + 2.0f);
        g.DrawLine(&pen, cx - 2.0f, y, cx + 2.0f, y);

        // Can body
        Gdiplus::RectF can(x, y + 2.5f, w, h - 2.5f);
        DrawRoundedRect(g, can, 2.0f, Gdiplus::Color(0,0,0,0), color, 1.3f);

        // Vertical rib lines
        g.DrawLine(&pen, cx - 2.0f, y + 5.0f, cx - 2.0f, y + h - 2.0f);
        g.DrawLine(&pen, cx + 2.0f, y + 5.0f, cx + 2.0f, y + h - 2.0f);
    }

    // Download Arrow (Import)
    inline void DrawDownloadArrowIcon(Gdiplus::Graphics& g, float cx, float cy, float size, const Gdiplus::Color& color) {
        Gdiplus::Pen pen(color, 1.5f);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);

        float s = size * 0.45f;
        // Downward vertical line
        g.DrawLine(&pen, cx, cy - s, cx, cy + s * 0.5f);
        // Arrow head
        g.DrawLine(&pen, cx - s * 0.6f, cy + s * 0.0f, cx, cy + s * 0.5f);
        g.DrawLine(&pen, cx + s * 0.6f, cy + s * 0.0f, cx, cy + s * 0.5f);
        // Tray bottom
        g.DrawLine(&pen, cx - s * 0.8f, cy + s, cx + s * 0.8f, cy + s);
    }

    // Upload Arrow (Export)
    inline void DrawUploadArrowIcon(Gdiplus::Graphics& g, float cx, float cy, float size, const Gdiplus::Color& color) {
        Gdiplus::Pen pen(color, 1.5f);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);

        float s = size * 0.45f;
        // Upward vertical line
        g.DrawLine(&pen, cx, cy + s * 0.5f, cx, cy - s);
        // Arrow head
        g.DrawLine(&pen, cx - s * 0.6f, cy - s * 0.5f, cx, cy - s);
        g.DrawLine(&pen, cx + s * 0.6f, cy - s * 0.5f, cx, cy - s);
        // Tray bottom
        g.DrawLine(&pen, cx - s * 0.8f, cy + s, cx + s * 0.8f, cy + s);
    }

    // Backup Floppy/Disk Icon
    inline void DrawBackupIcon(Gdiplus::Graphics& g, float cx, float cy, float size, const Gdiplus::Color& color) {
        float w = size * 0.75f;
        float h = size * 0.75f;
        float x = cx - w * 0.5f;
        float y = cy - h * 0.5f;

        Gdiplus::Pen pen(color, 1.4f);
        Gdiplus::RectF disk(x, y, w, h);
        DrawRoundedRect(g, disk, 2.0f, Gdiplus::Color(0,0,0,0), color, 1.4f);

        // Shutter rectangle
        Gdiplus::RectF shutter(x + 2.0f, y, w - 4.0f, h * 0.40f);
        DrawRoundedRect(g, shutter, 1.0f, Gdiplus::Color(0,0,0,0), color, 1.2f);
    }

    // Refresh / Circular Arrow (Flush DNS / Restore)
    inline void DrawRefreshIcon(Gdiplus::Graphics& g, float cx, float cy, float size, const Gdiplus::Color& color) {
        Gdiplus::Pen pen(color, 1.5f);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);

        float r = size * 0.40f;
        // 270 degree arc
        g.DrawArc(&pen, cx - r, cy - r, r * 2.0f, r * 2.0f, 45.0f, 270.0f);

        // Arrow head at top-right
        float ax = cx + r * 0.707f;
        float ay = cy - r * 0.707f;
        g.DrawLine(&pen, ax, ay, ax + 3.0f, ay - 3.0f);
        g.DrawLine(&pen, ax, ay, ax - 1.0f, ay - 4.0f);
    }

    // Draw Vector Folder Icon
    inline void DrawFolderIcon(Gdiplus::Graphics& g, float x, float y, const Gdiplus::Color& color) {
        Gdiplus::SolidBrush brush(color);
        g.FillRectangle(&brush, x, y, 5.0f, 2.0f);
        Gdiplus::RectF body(x, y + 2.0f, 13.0f, 9.0f);
        DrawRoundedRect(g, body, 1.5f, color, Gdiplus::Color(0, 0, 0, 0), 0);
    }

    // Draw Down Chevron
    inline void DrawDownChevron(Gdiplus::Graphics& g, float centerX, float centerY, const Gdiplus::Color& color) {
        Gdiplus::Pen pen(color, 1.5f);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        g.DrawLine(&pen, centerX - 3.5f, centerY - 1.5f, centerX, centerY + 2.0f);
        g.DrawLine(&pen, centerX, centerY + 2.0f, centerX + 3.5f, centerY - 1.5f);
    }

} // namespace UITheme
