#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

namespace UITheme {

    // Modern Dark Palette
    namespace Colors {
        const Gdiplus::Color BgMain(255, 18, 19, 26);          // #12131A
        const Gdiplus::Color BgHeader(255, 24, 26, 36);        // #181A24
        const Gdiplus::Color BgCard(255, 29, 32, 45);          // #1D202D
        const Gdiplus::Color BgCardHover(255, 37, 41, 57);     // #252939
        const Gdiplus::Color BgInput(255, 21, 23, 32);         // #151720
        const Gdiplus::Color BorderDark(255, 42, 45, 62);      // #2A2D3E
        const Gdiplus::Color BorderFocus(255, 99, 102, 241);   // Indigo #6366F1
        
        const Gdiplus::Color TextPrimary(255, 241, 245, 249);  // #F1F5F9
        const Gdiplus::Color TextSecondary(255, 148, 163, 184);// #94A3B8
        const Gdiplus::Color TextMuted(255, 100, 116, 139);    // #64748B
        
        const Gdiplus::Color Primary(255, 79, 70, 229);        // Indigo #4F46E5
        const Gdiplus::Color PrimaryHover(255, 99, 102, 241);  // #6366F1
        const Gdiplus::Color PrimaryText(255, 255, 255, 255);
        
        const Gdiplus::Color ActiveGreen(255, 16, 185, 129);   // Emerald #10B981
        const Gdiplus::Color ActiveGreenBg(255, 6, 78, 59);    // #064E3B
        
        const Gdiplus::Color DisabledGray(255, 100, 116, 139); // #64748B
        const Gdiplus::Color DisabledGrayBg(255, 30, 41, 59);  // #1E293B
        
        const Gdiplus::Color Danger(255, 239, 68, 68);         // Rose #EF4444
        const Gdiplus::Color DangerHover(255, 220, 38, 38);
        const Gdiplus::Color DangerBg(255, 69, 10, 10);
        
        const Gdiplus::Color AccentCyan(255, 56, 189, 248);    // #38BDF8
        const Gdiplus::Color AccentAmber(255, 245, 158, 11);   // #F59E0B
        
        const Gdiplus::Color ScrollTrack(255, 24, 26, 36);
        const Gdiplus::Color ScrollThumb(255, 51, 65, 85);
        const Gdiplus::Color ScrollThumbHover(255, 71, 85, 105);
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

        Gdiplus::SolidBrush fillBrush(fillColor);
        g.FillPath(&fillBrush, &path);

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
            trackColor = isHovered ? Gdiplus::Color(255, 16, 185, 129) : Gdiplus::Color(255, 5, 150, 105);
        } else {
            trackColor = isHovered ? Gdiplus::Color(255, 71, 85, 105) : Gdiplus::Color(255, 51, 65, 85);
        }

        DrawRoundedRect(g, trackRect, radius, trackColor, Gdiplus::Color(0, 0, 0, 0), 0);

        // Thumb circle
        float padding = 3.0f;
        float thumbDiameter = height - (padding * 2.0f);
        float thumbX = isOn ? (x + width - padding - thumbDiameter) : (x + padding);
        float thumbY = y + padding;

        Gdiplus::SolidBrush thumbBrush(Gdiplus::Color(255, 255, 255, 255));
        g.FillEllipse(&thumbBrush, thumbX, thumbY, thumbDiameter, thumbDiameter);
    }

    // Draw pill badge
    inline void DrawBadge(Gdiplus::Graphics& g, const std::wstring& text, float x, float y, const Gdiplus::Color& textColor, const Gdiplus::Color& bgColor, Gdiplus::Font* font, float paddingH = 8.0f, float paddingV = 3.0f) {
        Gdiplus::RectF layoutRect(0, 0, 1000, 100);
        Gdiplus::RectF boundRect;
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        g.MeasureString(text.c_str(), -1, font, layoutRect, &sf, &boundRect);

        float badgeW = boundRect.Width + paddingH * 2.0f;
        float badgeH = boundRect.Height + paddingV * 2.0f;
        Gdiplus::RectF badgeRect(x, y, badgeW, badgeH);

        DrawRoundedRect(g, badgeRect, badgeH / 2.0f, bgColor, Gdiplus::Color(0, 0, 0, 0), 0);

        Gdiplus::SolidBrush textBrush(textColor);
        g.DrawString(text.c_str(), -1, font, badgeRect, &sf, &textBrush);
    }

} // namespace UITheme
