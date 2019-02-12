///////////////////////////////////////////////////////////////////////////////
// Name:        src/ribbon/gallery.cpp
// Purpose:     Ribbon control which displays a gallery of items to choose from
// Author:      Peter Cawley
// Modified by:
// Created:     2009-07-22
// Copyright:   (C) Peter Cawley
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#ifdef __BORLANDC__
    #pragma hdrstop
#endif

#if wxUSE_RIBBON

#include "wx/ribbon/gallery.h"
#include "wx/ribbon/art.h"
#include "wx/ribbon/bar.h"
#include "wx/dcbuffer.h"
#include "wx/clntdata.h"

#ifndef WX_PRECOMP
#endif

#ifdef __WXMSW__
#include "wx/msw/private.h"
#endif

wxDEFINE_EVENT(wxEVT_RIBBONGALLERY_HOVER_CHANGED, wxRibbonGalleryEvent);
wxDEFINE_EVENT(wxEVT_RIBBONGALLERY_SELECTED, wxRibbonGalleryEvent);
wxDEFINE_EVENT(wxEVT_RIBBONGALLERY_CLICKED, wxRibbonGalleryEvent);

wxIMPLEMENT_DYNAMIC_CLASS(wxRibbonGalleryEvent, wxCommandEvent);
wxIMPLEMENT_CLASS(wxRibbonGallery, wxRibbonControl);

class wxRibbonGalleryItem
{
public:
    wxRibbonGalleryItem()
    {
        m_id = 0;
        m_is_visible = false;
        m_is_enabled = true;
    }

    void SetId(int id) {m_id = id;}
    int GetId() const { return m_id; }
    void SetBitmap(const wxBitmap& bitmap) {m_bitmap = bitmap;}
    const wxBitmap& GetBitmap() const {return m_bitmap;}
    void SetDisabledBitmap(const wxBitmap& bitmap) {m_bitmapDisabled = bitmap;}
    const wxBitmap& GetDisabledBitmap() const {return m_bitmapDisabled;}
    void SetIsVisible(bool visible) {m_is_visible = visible;}
    void SetPosition(int x, int y, const wxSize& size)
    {
        m_position = wxRect(wxPoint(x, y), size);
    }
    bool IsVisible() const {return m_is_visible;}
    const wxRect& GetPosition() const {return m_position;}

    void SetClientObject(wxClientData *data) {m_client_data.SetClientObject(data);}
    wxClientData *GetClientObject() const {return m_client_data.GetClientObject();}
    void SetClientData(void *data) {m_client_data.SetClientData(data);}
    void *GetClientData() const {return m_client_data.GetClientData();}
    wxString GetHelpString() const { return m_strHelp; }
    void SetHelpString(const wxString& strHelp) { m_strHelp = strHelp; }
    wxString GetName() const { return m_strName; }
    void SetName(const wxString& strName) { m_strName = strName; }
    bool IsEnabled() const { return m_is_enabled; }
    void SetEnabled(bool is_enabled) { m_is_enabled = is_enabled; }

protected:
    wxBitmap m_bitmap;
    wxBitmap m_bitmapDisabled;
    wxClientDataContainer m_client_data;
    wxRect m_position;
    int m_id;
    bool m_is_visible;
    bool m_is_enabled;
    wxString m_strName;
    wxString m_strHelp;
};

wxBEGIN_EVENT_TABLE(wxRibbonGallery, wxRibbonControl)
    EVT_ENTER_WINDOW(wxRibbonGallery::OnMouseEnter)
    EVT_ERASE_BACKGROUND(wxRibbonGallery::OnEraseBackground)
    EVT_LEAVE_WINDOW(wxRibbonGallery::OnMouseLeave)
    EVT_LEFT_DOWN(wxRibbonGallery::OnMouseDown)
    EVT_LEFT_UP(wxRibbonGallery::OnMouseUp)
    EVT_LEFT_DCLICK(wxRibbonGallery::OnMouseDClick)
    EVT_MOTION(wxRibbonGallery::OnMouseMove)
    EVT_PAINT(wxRibbonGallery::OnPaint)
    EVT_SIZE(wxRibbonGallery::OnSize)
wxEND_EVENT_TABLE()

wxRibbonGallery::wxRibbonGallery()
{
    m_bAllowMouseSelection = true;
    m_nItemsPerRowMin = 1;
    m_nItemsPerRowBest = 3;
}

wxRibbonGallery::wxRibbonGallery(wxWindow* parent,
                  wxWindowID id,
                  const wxPoint& pos,
                  const wxSize& size,
                  long style)
    : wxRibbonControl(parent, id, pos, size, wxBORDER_NONE)
{
    CommonInit(style);
}

wxRibbonGallery::~wxRibbonGallery()
{
    Clear();
}

bool wxRibbonGallery::Create(wxWindow* parent,
                wxWindowID id,
                const wxPoint& pos,
                const wxSize& size,
                long style)
{
    if(!wxRibbonControl::Create(parent, id, pos, size, wxBORDER_NONE))
    {
        return false;
    }

    CommonInit(style);
    return true;
}

void wxRibbonGallery::CommonInit(long WXUNUSED(style))
{
    m_bAllowMouseSelection = true;
    m_nItemsPerRowMin = 1;
    m_nItemsPerRowBest = 3;
    m_selected_item = NULL;
    m_hovered_item = NULL;
    m_active_item = NULL;
    m_scroll_up_button_rect = wxRect(0, 0, 0, 0);
    m_scroll_down_button_rect = wxRect(0, 0, 0, 0);
    m_extension_button_rect = wxRect(0, 0, 0, 0);
    m_mouse_active_rect = NULL;
    m_bitmap_size = wxSize(64, 32);
    m_bitmap_padded_size = m_bitmap_size;
    m_item_separation_x = 0;
    m_item_separation_y = 0;
    m_scroll_amount = 0;
    m_scroll_limit = 0;
    m_up_button_state = wxRIBBON_GALLERY_BUTTON_DISABLED;
    m_down_button_state = wxRIBBON_GALLERY_BUTTON_NORMAL;
    m_extension_button_state = wxRIBBON_GALLERY_BUTTON_NORMAL;
    m_hovered = false;

    SetBackgroundStyle(wxBG_STYLE_PAINT);
}

void wxRibbonGallery::OnMouseEnter(wxMouseEvent& evt)
{
    m_hovered = true;
    if(m_mouse_active_rect != NULL && !evt.LeftIsDown())
    {
        m_mouse_active_rect = NULL;
        m_active_item = NULL;
    }
    Refresh(false);
}

void wxRibbonGallery::OnMouseMove(wxMouseEvent& evt)
{
    bool refresh = false;
    wxPoint pos = evt.GetPosition();

    if(TestButtonHover(m_scroll_up_button_rect, pos, &m_up_button_state))
        refresh = true;
    if(TestButtonHover(m_scroll_down_button_rect, pos, &m_down_button_state))
        refresh = true;
    if(TestButtonHover(m_extension_button_rect, pos, &m_extension_button_state))
        refresh = true;

    wxRibbonGalleryItem *hovered_item = NULL;
    wxRibbonGalleryItem *active_item = NULL;
    if(m_client_rect.Contains(pos))
    {
        if(m_art && m_art->GetFlags() & wxRIBBON_BAR_FLOW_VERTICAL)
            pos.x += m_scroll_amount;
        else
            pos.y += m_scroll_amount;

        size_t item_count = m_items.Count();
        size_t item_i;
        for(item_i = 0; item_i < item_count; ++item_i)
        {
            wxRibbonGalleryItem *item = m_items.Item(item_i);
            if(!item->IsVisible())
                continue;
            if (!item->IsEnabled())
                continue;

            if(item->GetPosition().Contains(pos))
            {
                if(m_mouse_active_rect == &item->GetPosition())
                    active_item = item;
                hovered_item = item;
                break;
            }
        }
    }
    if(active_item != m_active_item)
    {
        m_active_item = active_item;
        refresh = true;
    }
    if(hovered_item != m_hovered_item)
    {
#if wxUSE_TOOLTIPS
        if(hovered_item && !hovered_item->GetHelpString().empty())
        {
            SetToolTip(hovered_item->GetHelpString());
        }
        else
        {
            UnsetToolTip();
        }
#endif
        m_hovered_item = hovered_item;
        wxRibbonGalleryEvent notification(
            wxEVT_RIBBONGALLERY_HOVER_CHANGED, GetId());
        notification.SetEventObject(this);
        notification.SetGallery(this);
        notification.SetGalleryItem(hovered_item);
        ProcessWindowEvent(notification);
        refresh = true;
    }

    if(refresh)
        Refresh(false);
}

bool wxRibbonGallery::TestButtonHover(const wxRect& rect, wxPoint pos,
        wxRibbonGalleryButtonState* state)
{
    if(*state == wxRIBBON_GALLERY_BUTTON_DISABLED)
        return false;

    wxRibbonGalleryButtonState new_state;
    if(rect.Contains(pos))
    {
        if(m_mouse_active_rect == &rect)
            new_state = wxRIBBON_GALLERY_BUTTON_ACTIVE;
        else
            new_state = wxRIBBON_GALLERY_BUTTON_HOVERED;
    }
    else
        new_state = wxRIBBON_GALLERY_BUTTON_NORMAL;

    if(new_state != *state)
    {
        *state = new_state;
        return true;
    }
    else
    {
        return false;
    }
}

void wxRibbonGallery::OnMouseLeave(wxMouseEvent& WXUNUSED(evt))
{
    m_hovered = false;
    m_active_item = NULL;
    if(m_up_button_state != wxRIBBON_GALLERY_BUTTON_DISABLED)
        m_up_button_state = wxRIBBON_GALLERY_BUTTON_NORMAL;
    if(m_down_button_state != wxRIBBON_GALLERY_BUTTON_DISABLED)
        m_down_button_state = wxRIBBON_GALLERY_BUTTON_NORMAL;
    if(m_extension_button_state != wxRIBBON_GALLERY_BUTTON_DISABLED)
        m_extension_button_state = wxRIBBON_GALLERY_BUTTON_NORMAL;
    if(m_hovered_item != NULL)
    {
        m_hovered_item = NULL;
#if wxUSE_TOOLTIPS
        UnsetToolTip();
#endif
        wxRibbonGalleryEvent notification(
            wxEVT_RIBBONGALLERY_HOVER_CHANGED, GetId());
        notification.SetEventObject(this);
        notification.SetGallery(this);
        ProcessWindowEvent(notification);
    }
    Refresh(false);
}

void wxRibbonGallery::OnMouseDown(wxMouseEvent& evt)
{
    wxPoint pos = evt.GetPosition();
    m_mouse_active_rect = NULL;
    if(m_client_rect.Contains(pos))
    {
        if(m_art && m_art->GetFlags() & wxRIBBON_BAR_FLOW_VERTICAL)
            pos.x += m_scroll_amount;
        else
            pos.y += m_scroll_amount;
        size_t item_count = m_items.Count();
        size_t item_i;
        for(item_i = 0; item_i < item_count; ++item_i)
        {
            wxRibbonGalleryItem *item = m_items.Item(item_i);
            if(!item->IsVisible())
                continue;
            if (!item->IsEnabled())
                continue;

            const wxRect& rect = item->GetPosition();
            if(rect.Contains(pos))
            {
                m_active_item = item;
                m_mouse_active_rect = &rect;
                break;
            }
        }
    }
    else if(m_scroll_up_button_rect.Contains(pos))
    {
        if(m_up_button_state != wxRIBBON_GALLERY_BUTTON_DISABLED)
        {
            m_mouse_active_rect = &m_scroll_up_button_rect;
            m_up_button_state = wxRIBBON_GALLERY_BUTTON_ACTIVE;
        }
    }
    else if(m_scroll_down_button_rect.Contains(pos))
    {
        if(m_down_button_state != wxRIBBON_GALLERY_BUTTON_DISABLED)
        {
            m_mouse_active_rect = &m_scroll_down_button_rect;
            m_down_button_state = wxRIBBON_GALLERY_BUTTON_ACTIVE;
        }
    }
    else if(m_extension_button_rect.Contains(pos))
    {
        if(m_extension_button_state != wxRIBBON_GALLERY_BUTTON_DISABLED)
        {
            m_mouse_active_rect = &m_extension_button_rect;
            m_extension_button_state = wxRIBBON_GALLERY_BUTTON_ACTIVE;
        }
    }
    if(m_mouse_active_rect != NULL)
        Refresh(false);
}

void wxRibbonGallery::OnMouseUp(wxMouseEvent& evt)
{
    if(m_mouse_active_rect != NULL)
    {
        wxPoint pos = evt.GetPosition();
        if(m_active_item)
        {
            if(m_art && m_art->GetFlags() & wxRIBBON_BAR_FLOW_VERTICAL)
                pos.x += m_scroll_amount;
            else
                pos.y += m_scroll_amount;
        }
        if(m_mouse_active_rect->Contains(pos))
        {
            if(m_mouse_active_rect == &m_scroll_up_button_rect)
            {
                m_up_button_state = wxRIBBON_GALLERY_BUTTON_HOVERED;
                ScrollLines(-1);
            }
            else if(m_mouse_active_rect == &m_scroll_down_button_rect)
            {
                m_down_button_state = wxRIBBON_GALLERY_BUTTON_HOVERED;
                ScrollLines(1);
            }
            else if(m_mouse_active_rect == &m_extension_button_rect)
            {
                m_extension_button_state = wxRIBBON_GALLERY_BUTTON_HOVERED;
                wxCommandEvent notification(wxEVT_BUTTON,
                    GetId());
                notification.SetEventObject(this);
                ProcessWindowEvent(notification);
            }
            else if(m_active_item != NULL)
            {
                if(m_bAllowMouseSelection && m_selected_item != m_active_item)
                {
                    m_selected_item = m_active_item;
                    wxRibbonGalleryEvent notification(
                        wxEVT_RIBBONGALLERY_SELECTED, GetId());
                    notification.SetEventObject(this);
                    notification.SetGallery(this);
                    notification.SetGalleryItem(m_selected_item);
                    ProcessWindowEvent(notification);
                }

                wxRibbonGalleryEvent notification(
                    wxEVT_RIBBONGALLERY_CLICKED, GetId());
                notification.SetEventObject(this);
                notification.SetGallery(this);
                notification.SetGalleryItem(m_active_item);
                ProcessWindowEvent(notification);
            }
        }
        m_mouse_active_rect = NULL;
        m_active_item = NULL;
        Refresh(false);
    }
}

void wxRibbonGallery::OnMouseDClick(wxMouseEvent& evt)
{
    // The 2nd click of a double-click should be handled as a click in the
    // same way as the 1st click of the double-click. This is useful for
    // scrolling through the gallery.
    OnMouseDown(evt);
    OnMouseUp(evt);
}

void wxRibbonGallery::SetItemClientObject(wxRibbonGalleryItem* itm,
                                          wxClientData* data)
{
    itm->SetClientObject(data);
}

wxClientData* wxRibbonGallery::GetItemClientObject(const wxRibbonGalleryItem* itm) const
{
    return itm->GetClientObject();
}

void wxRibbonGallery::SetItemClientData(wxRibbonGalleryItem* itm, void* data)
{
    itm->SetClientData(data);
}

void* wxRibbonGallery::GetItemClientData(const wxRibbonGalleryItem* itm) const
{
    return itm->GetClientData();
}

int wxRibbonGallery::GetItemId(const wxRibbonGalleryItem* item) const
{
    return item ? item->GetId() : wxID_ANY;
}

void wxRibbonGallery::SetItemName(wxRibbonGalleryItem* item, const wxString& strName)
{
    if (item)
        item->SetName(strName);
}

wxString wxRibbonGallery::GetItemName(wxRibbonGalleryItem* item) const
{
    if(item) 
        return item->GetName();
    return wxEmptyString;
}

void wxRibbonGallery::SetItemHelpString(wxRibbonGalleryItem* item, const wxString& strHelp)
{
    if (item)
        item->SetHelpString(strHelp);
}

wxString wxRibbonGallery::GetItemHelpString(wxRibbonGalleryItem* item) const
{
    if(item) 
        return item->GetHelpString();
    return wxEmptyString;
}

void wxRibbonGallery::SetItemBitmap(wxRibbonGalleryItem* item, const wxBitmap& bitmap)
{
    if (item)
        item->SetBitmap(bitmap);
}

wxBitmap wxRibbonGallery::GetItemBitmap(wxRibbonGalleryItem* item) const
{
    if (item)
        return item->GetBitmap();
    return wxNullBitmap;
}

void wxRibbonGallery::SetItemDisabledBitmap(wxRibbonGalleryItem* item, const wxBitmap& bitmap)
{
    if (item)
        item->SetDisabledBitmap(bitmap);
}

wxBitmap wxRibbonGallery::GetItemDisabledBitmap(wxRibbonGalleryItem* item) const
{
    if (item)
        return item->GetDisabledBitmap();
    return wxNullBitmap;
}

void wxRibbonGallery::SetItemEnabled(wxRibbonGalleryItem* item, bool bEnabled)
{
    if (item)
    {
        if (bEnabled == item->IsEnabled())
            return;
        item->SetEnabled(bEnabled);
        Refresh(false);
    }
}

bool wxRibbonGallery::GetItemEnabled(wxRibbonGalleryItem* item) const
{
    if (item)
        return item->IsEnabled();
    return false;
}

bool wxRibbonGallery::ScrollLines(int lines)
{
    if(m_scroll_limit == 0 || m_art == NULL)
        return false;

    return ScrollPixels(lines * GetScrollLineSize());
}

int wxRibbonGallery::GetScrollLineSize() const
{
    if(m_art == NULL)
        return 32;

    int line_size = m_bitmap_padded_size.GetHeight();
    if(m_art->GetFlags() & wxRIBBON_BAR_FLOW_VERTICAL)
        line_size = m_bitmap_padded_size.GetWidth();

    return line_size;
}

bool wxRibbonGallery::ScrollPixels(int pixels)
{
    if(m_scroll_limit == 0 || m_art == NULL)
        return false;

    if(pixels < 0)
    {
        if(m_scroll_amount > 0)
        {
            m_scroll_amount += pixels;
            if(m_scroll_amount <= 0)
            {
                m_scroll_amount = 0;
                m_up_button_state = wxRIBBON_GALLERY_BUTTON_DISABLED;
            }
            else if(m_up_button_state == wxRIBBON_GALLERY_BUTTON_DISABLED)
                m_up_button_state = wxRIBBON_GALLERY_BUTTON_NORMAL;
            if(m_down_button_state == wxRIBBON_GALLERY_BUTTON_DISABLED)
                m_down_button_state = wxRIBBON_GALLERY_BUTTON_NORMAL;
            return true;
        }
    }
    else if(pixels > 0)
    {
        if(m_scroll_amount < m_scroll_limit)
        {
            m_scroll_amount += pixels;
            if(m_scroll_amount >= m_scroll_limit)
            {
                m_scroll_amount = m_scroll_limit;
                m_down_button_state = wxRIBBON_GALLERY_BUTTON_DISABLED;
            }
            else if(m_down_button_state == wxRIBBON_GALLERY_BUTTON_DISABLED)
                m_down_button_state = wxRIBBON_GALLERY_BUTTON_NORMAL;
            if(m_up_button_state == wxRIBBON_GALLERY_BUTTON_DISABLED)
                m_up_button_state = wxRIBBON_GALLERY_BUTTON_NORMAL;
            return true;
        }
    }
    return false;
}

void wxRibbonGallery::EnsureVisible(const wxRibbonGalleryItem* item)
{
    if(item == NULL || !item->IsVisible() || IsEmpty())
        return;

    if(m_art->GetFlags() & wxRIBBON_BAR_FLOW_VERTICAL)
    {
        int x = item->GetPosition().GetLeft();
        int base_x = m_items.Item(0)->GetPosition().GetLeft();
        int delta = x - base_x - m_scroll_amount;
        ScrollLines(delta / m_bitmap_padded_size.GetWidth());
    }
    else
    {
        int y = item->GetPosition().GetTop();
        int base_y = m_items.Item(0)->GetPosition().GetTop();
        int delta = y - base_y - m_scroll_amount;
        ScrollLines(delta / m_bitmap_padded_size.GetHeight());
    }
}

bool wxRibbonGallery::IsHovered() const
{
    return m_hovered;
}

void wxRibbonGallery::OnEraseBackground(wxEraseEvent& WXUNUSED(evt))
{
    // All painting done in main paint handler to minimise flicker
}

void wxRibbonGallery::OnPaint(wxPaintEvent& WXUNUSED(evt))
{
    wxAutoBufferedPaintDC dc(this);
    if(m_art == NULL)
        return;

    m_art->DrawGalleryBackground(dc, this, GetSize());

    int padding_top = m_art->GetMetric(wxRIBBON_ART_GALLERY_BITMAP_PADDING_TOP_SIZE);
    dc.SetClippingRegion(m_client_rect);
    if (m_art)
        dc.SetFont(m_art->GetFont(wxRIBBON_ART_BUTTON_BAR_LABEL_FONT));

    bool offset_vertical = true;
    if(m_art->GetFlags() & wxRIBBON_BAR_FLOW_VERTICAL)
        offset_vertical = false;
    size_t item_count = m_items.Count();
    size_t item_i;
    for(item_i = 0; item_i < item_count; ++item_i)
    {
        wxRibbonGalleryItem *item = m_items.Item(item_i);
        if(!item->IsVisible())
            continue;

        const wxRect& pos = item->GetPosition();
        wxRect offset_pos(pos);
        if(offset_vertical)
            offset_pos.SetTop(offset_pos.GetTop() - m_scroll_amount);
        else
            offset_pos.SetLeft(offset_pos.GetLeft() - m_scroll_amount);
        m_art->DrawGalleryItemBackground(dc, this, offset_pos, item);

        wxBitmap bmp = item->GetBitmap();
        if (!item->IsEnabled())
        {
            if (item->GetDisabledBitmap().IsOk())
                bmp = item->GetDisabledBitmap();
            else if (bmp.IsOk())
            {
                wxImage image = bmp.ConvertToImage();
                if (image.IsOk())
                    image = image.ConvertToDisabled();
                if (image.IsOk())
                {
                    bmp = wxBitmap(image, 32, bmp.GetScaleFactor());
                    item->SetDisabledBitmap(bmp);
                }
            }
        }
        
        wxSize szBitmap = item->GetBitmap().GetScaledSize();
        dc.DrawBitmap(bmp, offset_pos.GetLeft() + (pos.width - szBitmap.x)/2,
            offset_pos.GetTop() + padding_top);

        wxString strName = item->GetName();
        if (strName != wxT("")) 
        {
            int tw, th;    
            dc.GetTextExtent(strName, &tw, &th);
            dc.DrawText(strName, offset_pos.GetLeft() + (pos.width - tw)/2,
            offset_pos.GetTop() + padding_top + szBitmap.y + padding_top);
        }
    }
}

void wxRibbonGallery::OnSize(wxSizeEvent& WXUNUSED(evt))
{
    Layout();
}

wxRibbonGalleryItem* wxRibbonGallery::Append(const wxBitmap& bitmap, int id, const wxString& strName /*= wxEmptyString*/, const wxString& strHelp /*= wxEmptyString*/)
{
    return Insert(-1, bitmap, id, strName, strHelp);
}

wxRibbonGalleryItem* wxRibbonGallery::Append(const wxBitmap& bitmap, int id,
                                             void* clientData, const wxString& strName /*= wxEmptyString*/, const wxString& strHelp /*= wxEmptyString*/)
{
    wxRibbonGalleryItem *item = Append(bitmap, id, strName, strHelp);
    item->SetClientData(clientData);
    return item;
}

wxRibbonGalleryItem* wxRibbonGallery::Append(const wxBitmap& bitmap, int id,
                                             wxClientData* clientData, const wxString& strName /*= wxEmptyString*/, const wxString& strHelp /*= wxEmptyString*/)
{
    wxRibbonGalleryItem *item = Append(bitmap, id, strName, strHelp);
    item->SetClientObject(clientData);
    return item;
}

wxRibbonGalleryItem* wxRibbonGallery::Insert(int pos, const wxBitmap& bitmap, int id, const wxString& strName /*= wxEmptyString*/, const wxString& strHelp /*= wxEmptyString*/)
{
    size_t unPos = (size_t)pos;
    if (unPos > m_items.GetCount())
        unPos = m_items.GetCount();

    wxASSERT(bitmap.IsOk());
    
    wxSize szItem = bitmap.GetScaledSize();
    
    if (strName != wxT(""))
    {
        int tw, th;
        wxClientDC dc(this);
        if (m_art)
            dc.SetFont(m_art->GetFont(wxRIBBON_ART_BUTTON_BAR_LABEL_FONT));
        dc.GetTextExtent(strName, &tw, &th);

        szItem.x = ::wxMax(tw, szItem.x);
        szItem.y += th;
        szItem.y += m_art->GetMetric(wxRIBBON_ART_GALLERY_BITMAP_PADDING_TOP_SIZE);
    }
    
    if (szItem.x > m_bitmap_size.x || szItem.y > m_bitmap_size.y || m_items.IsEmpty())
    {
        m_bitmap_size.IncTo(szItem);
        CalculateMinSize();
    }

    wxRibbonGalleryItem *item = new wxRibbonGalleryItem;
    item->SetId(id);
    item->SetBitmap(bitmap);
    item->SetName(strName);
    item->SetHelpString(strHelp);
    m_items.Insert(item, unPos);
    return item;
}

wxRibbonGalleryItem* wxRibbonGallery::Insert(int pos, const wxBitmap& bitmap, int id,
                                             void* clientData, const wxString& strName /*= wxEmptyString*/, const wxString& strHelp /*= wxEmptyString*/)
{
    wxRibbonGalleryItem *item = Insert(pos, bitmap, id, strName, strHelp);
    item->SetClientData(clientData);
    return item;
}

wxRibbonGalleryItem* wxRibbonGallery::Insert(int pos, const wxBitmap& bitmap, int id,
                                             wxClientData* clientData, const wxString& strName /*= wxEmptyString*/, const wxString& strHelp /*= wxEmptyString*/)
{
    wxRibbonGalleryItem *item = Insert(pos, bitmap, id, strName, strHelp);
    item->SetClientObject(clientData);
    return item;
}

void wxRibbonGallery::Remove(unsigned int pos)
{
    if (pos >= m_items.Count())
        return;
    wxRibbonGalleryItem *item = m_items.Item(pos);
    m_items.RemoveAt(pos);
    if (!item)
        return;
    if (item == m_selected_item)
        m_selected_item = NULL;
    if (item == m_hovered_item)
    {
        m_hovered_item = NULL;
#if wxUSE_TOOLTIPS
        UnsetToolTip();
#endif
    }
    if (item == m_active_item)
        m_active_item = NULL;
    if (m_mouse_active_rect == &item->GetPosition())
        m_mouse_active_rect = NULL;
    delete item;
}

int wxRibbonGallery::RemoveItemByID(int nID)
{
    size_t item_count = m_items.Count();
    size_t item_i;
    for (item_i = 0; item_i < item_count; ++item_i)
    {
        wxRibbonGalleryItem *item = m_items.Item(item_i);
        if (item->GetId() == nID)
        {
            m_items.RemoveAt(item_i);
            if (item == m_selected_item)
                m_selected_item = NULL;
            if (item == m_hovered_item)
            {
                m_hovered_item = NULL;
#if wxUSE_TOOLTIPS
                UnsetToolTip();
#endif
            }
            if (item == m_active_item)
                m_active_item = NULL;
            if (m_mouse_active_rect == &item->GetPosition())
                m_mouse_active_rect = NULL;
            delete item;
            item = NULL;
            return item_i;
        }
    }
    return -1;
}

void wxRibbonGallery::Clear()
{
    size_t item_count = m_items.Count();
    size_t item_i;
    for(item_i = 0; item_i < item_count; ++item_i)
    {
        wxRibbonGalleryItem *item = m_items.Item(item_i);
        delete item;
    }
    m_items.Clear();
    m_bitmap_size = wxSize(64, 32);
}

bool wxRibbonGallery::IsSizingContinuous() const
{
    return false;
}

void wxRibbonGallery::CalculateMinSize()
{
    if(m_art == NULL || !m_bitmap_size.IsFullySpecified())
    {
        SetMinSize(wxSize(20, 20));
    }
    else
    {
        m_bitmap_padded_size = m_bitmap_size;
        m_bitmap_padded_size.IncBy(
            m_art->GetMetric(wxRIBBON_ART_GALLERY_BITMAP_PADDING_LEFT_SIZE) +
            m_art->GetMetric(wxRIBBON_ART_GALLERY_BITMAP_PADDING_RIGHT_SIZE),
            m_art->GetMetric(wxRIBBON_ART_GALLERY_BITMAP_PADDING_TOP_SIZE) +
            m_art->GetMetric(wxRIBBON_ART_GALLERY_BITMAP_PADDING_BOTTOM_SIZE));

        wxMemoryDC dc;
        SetMinSize(m_art->GetGallerySize(dc, this, wxSize(m_nItemsPerRowMin * m_bitmap_padded_size.x , m_bitmap_padded_size.y)));

        // The best size is displaying several items
        m_best_size = m_bitmap_padded_size;
        m_best_size.x *= m_nItemsPerRowBest;
        m_best_size = m_art->GetGallerySize(dc, this, m_best_size);
    }
}

bool wxRibbonGallery::Realize()
{
    CalculateMinSize();
    return Layout();
}

bool wxRibbonGallery::Layout()
{
    if(m_art == NULL)
        return false;

    wxMemoryDC dc;
    wxPoint origin;
    wxSize client_size = m_art->GetGalleryClientSize(dc, this, GetSize(),
        &origin, &m_scroll_up_button_rect, &m_scroll_down_button_rect,
        &m_extension_button_rect);
    m_client_rect = wxRect(origin, client_size);

    int x_cursor = 0;
    int y_cursor = 0;

    size_t item_count = m_items.Count();
    size_t item_i;
    long art_flags = m_art->GetFlags();
    for(item_i = 0; item_i < item_count; ++item_i)
    {
        wxRibbonGalleryItem *item = m_items.Item(item_i);
        item->SetIsVisible(true);
        if(art_flags & wxRIBBON_BAR_FLOW_VERTICAL)
        {
            if(y_cursor + m_bitmap_padded_size.y > client_size.GetHeight())
            {
                if(y_cursor == 0)
                    break;
                y_cursor = 0;
                x_cursor += m_bitmap_padded_size.x;
            }
            item->SetPosition(origin.x + x_cursor, origin.y + y_cursor,
                m_bitmap_padded_size);
            y_cursor += m_bitmap_padded_size.y;
        }
        else
        {
            if(x_cursor + m_bitmap_padded_size.x > client_size.GetWidth())
            {
                if(x_cursor == 0)
                    break;
                x_cursor = 0;
                y_cursor += m_bitmap_padded_size.y;
            }
            item->SetPosition(origin.x + x_cursor, origin.y + y_cursor,
                m_bitmap_padded_size);
            x_cursor += m_bitmap_padded_size.x;
        }
    }
    for(; item_i < item_count; ++item_i)
    {
        wxRibbonGalleryItem *item = m_items.Item(item_i);
        item->SetIsVisible(false);
    }
    if(art_flags & wxRIBBON_BAR_FLOW_VERTICAL)
        m_scroll_limit = x_cursor;
    else
        m_scroll_limit = y_cursor;
    if(m_scroll_amount >= m_scroll_limit)
    {
        m_scroll_amount = m_scroll_limit;
        m_down_button_state = wxRIBBON_GALLERY_BUTTON_DISABLED;
    }
    else if(m_down_button_state == wxRIBBON_GALLERY_BUTTON_DISABLED)
        m_down_button_state = wxRIBBON_GALLERY_BUTTON_NORMAL;

    if(m_scroll_amount <= 0)
    {
        m_scroll_amount = 0;
        m_up_button_state = wxRIBBON_GALLERY_BUTTON_DISABLED;
    }
    else if(m_up_button_state == wxRIBBON_GALLERY_BUTTON_DISABLED)
        m_up_button_state = wxRIBBON_GALLERY_BUTTON_NORMAL;

    return true;
}

wxSize wxRibbonGallery::DoGetBestSize() const
{
    return m_best_size;
}

wxSize wxRibbonGallery::DoGetNextSmallerSize(wxOrientation direction,
                                        wxSize relative_to) const
{
    if(m_art == NULL)
        return relative_to;

    wxMemoryDC dc;

    wxSize client = m_art->GetGalleryClientSize(dc, this, relative_to, NULL,
        NULL, NULL, NULL);
    switch(direction)
    {
    case wxHORIZONTAL:
        client.DecBy(1, 0);
        break;
    case wxVERTICAL:
        client.DecBy(0, 1);
        break;
    case wxBOTH:
        client.DecBy(1, 1);
        break;
    }
    if(client.GetWidth() < 0 || client.GetHeight() < 0)
        return relative_to;

    client.x = (client.x / m_bitmap_padded_size.x) * m_bitmap_padded_size.x;
    client.y = (client.y / m_bitmap_padded_size.y) * m_bitmap_padded_size.y;

    wxSize size = m_art->GetGallerySize(dc, this, client);
    wxSize minimum = GetMinSize();

    if(size.GetWidth() < minimum.GetWidth() ||
        size.GetHeight() < minimum.GetHeight())
    {
        return relative_to;
    }

    switch(direction)
    {
    case wxHORIZONTAL:
        size.SetHeight(relative_to.GetHeight());
        break;
    case wxVERTICAL:
        size.SetWidth(relative_to.GetWidth());
        break;
    default:
        break;
    }

    return size;
}

wxSize wxRibbonGallery::DoGetNextLargerSize(wxOrientation direction,
                                       wxSize relative_to) const
{
    if(m_art == NULL)
        return relative_to;

    wxMemoryDC dc;

    wxSize client = m_art->GetGalleryClientSize(dc, this, relative_to, NULL,
        NULL, NULL, NULL);

    // No need to grow if the given size can already display every item
    int nitems = (client.GetWidth() / m_bitmap_padded_size.x) *
        (client.GetHeight() / m_bitmap_padded_size.y);
    if(nitems >= (int)m_items.GetCount())
        return relative_to;

    switch(direction)
    {
    case wxHORIZONTAL:
        client.IncBy(m_bitmap_padded_size.x, 0);
        break;
    case wxVERTICAL:
        client.IncBy(0, m_bitmap_padded_size.y);
        break;
    case wxBOTH:
        client.IncBy(m_bitmap_padded_size);
        break;
    }

    client.x = (client.x / m_bitmap_padded_size.x) * m_bitmap_padded_size.x;
    client.y = (client.y / m_bitmap_padded_size.y) * m_bitmap_padded_size.y;

    wxSize size = m_art->GetGallerySize(dc, this, client);
    wxSize minimum = GetMinSize();

    if(size.GetWidth() < minimum.GetWidth() ||
        size.GetHeight() < minimum.GetHeight())
    {
        return relative_to;
    }
    if (size.GetWidth() > m_best_size.GetWidth() ||
        size.GetHeight() > m_best_size.GetHeight())
    {
        return relative_to;
    }

    switch(direction)
    {
    case wxHORIZONTAL:
        size.SetHeight(relative_to.GetHeight());
        break;
    case wxVERTICAL:
        size.SetWidth(relative_to.GetWidth());
        break;
    default:
        break;
    }

    return size;
}

bool wxRibbonGallery::IsEmpty() const
{
    return m_items.IsEmpty();
}

unsigned int wxRibbonGallery::GetCount() const
{
    return (unsigned int)m_items.GetCount();
}

wxRibbonGalleryItem* wxRibbonGallery::GetItem(unsigned int n)
{
    if(n >= GetCount())
        return NULL;
    return m_items.Item(n);
}

wxRibbonGalleryItem* wxRibbonGallery::GetItemByID(int nID)
{
    size_t item_count = m_items.Count();
    size_t item_i;
    for(item_i = 0; item_i < item_count; ++item_i)
    {
        wxRibbonGalleryItem *item = m_items.Item(item_i);
        if (item->GetId() == nID)
            return item;
    }
    return NULL;
}

void wxRibbonGallery::SetSelection(wxRibbonGalleryItem* item)
{
    if(item != m_selected_item)
    {
        m_selected_item = item;
        Refresh(false);
    }
}

wxRibbonGalleryItem* wxRibbonGallery::GetSelection() const
{
    return m_selected_item;
}

wxRibbonGalleryItem* wxRibbonGallery::GetHoveredItem() const
{
    return m_hovered_item;
}

wxRibbonGalleryItem* wxRibbonGallery::GetActiveItem() const
{
    return m_active_item;
}

wxRibbonGalleryButtonState wxRibbonGallery::GetUpButtonState() const
{
    return m_up_button_state;
}

wxRibbonGalleryButtonState wxRibbonGallery::GetDownButtonState() const
{
    return m_down_button_state;
}

wxRibbonGalleryButtonState wxRibbonGallery::GetExtensionButtonState() const
{
    return m_extension_button_state;
}

void wxRibbonGallery::AllowSelectItemByMouse(bool bAllow /*= true*/)
{
    m_bAllowMouseSelection = bAllow;
}

void wxRibbonGallery::SetItemsPerRow(int nMinItems, int nBestItems)
{
    m_nItemsPerRowMin = nMinItems;
    m_nItemsPerRowBest = nBestItems;
}

wxSize wxRibbonGallery::GetItemSize() const
{
    return m_bitmap_size;
}

void wxRibbonGallery::EnableAllItems()
{
    size_t item_count = m_items.Count();
    size_t item_i;
    bool bRefresh = false;
    for (item_i = 0; item_i < item_count; ++item_i)
    {
        wxRibbonGalleryItem *item = m_items.Item(item_i);
        if (!item->IsEnabled())
        {
            item->SetEnabled(true);
            bRefresh = true;
        }
    }
    if (bRefresh)
        Refresh(false);
}

void wxRibbonGallery::DisableAllItems()
{
    size_t item_count = m_items.Count();
    size_t item_i;
    bool bRefresh = false;
    for (item_i = 0; item_i < item_count; ++item_i)
    {
        wxRibbonGalleryItem *item = m_items.Item(item_i);
        if (item->IsEnabled())
        {
            item->SetEnabled(false);
            bRefresh = true;
        }
    }
    if (bRefresh)
        Refresh(false);
}

int wxRibbonGallery::GetItemIndex(wxRibbonGalleryItem* pItem)
{
    int nRet = -1;
    size_t item_count = m_items.Count();
    size_t item_i;
    int nCount = 0;
    for (item_i = 0; item_i < item_count; ++item_i)
    {
        wxRibbonGalleryItem *item = m_items.Item(item_i);
        if (pItem == item)
        {
            nRet = nCount;
            break;
        }
        nCount++;
    }
    return nRet;
}

wxRect wxRibbonGallery::GetItemRect(int nItemIndex) const
{
    wxRect rcItem;
    size_t item_count = m_items.Count();
    size_t item_i;
    int nCount = 0;
    for (item_i = 0; item_i < item_count; ++item_i)
    {
        wxRibbonGalleryItem *item = m_items.Item(item_i);
        if (nCount == nItemIndex)
        {
            rcItem = item->GetPosition();
            break;
        }
        nCount++;
    }
    return rcItem;
}

wxRibbonGalleryItem* wxRibbonGallery::FindItemByPos(const wxPoint& pt)
{
    size_t item_count = m_items.Count();
    size_t item_i;
    wxRibbonGalleryItem* pHoverItem = NULL;
    for (item_i = 0; item_i < item_count; ++item_i)
    {
        wxRibbonGalleryItem *item = m_items.Item(item_i);
        if (item->GetPosition().Contains(pt))
        {
            pHoverItem = item;
            break;
        }
    }
    return pHoverItem;
}

#endif // wxUSE_RIBBON
