/////////////////////////////////////////////////////////////////////////////
// Name:        src/osx/core/dcmemory.cpp
// Purpose:     wxMemoryDC class
// Author:      Stefan Csomor
// Modified by:
// Created:     01/02/97
// Copyright:   (c) Stefan Csomor
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/dcmemory.h"
#include "wx/graphics.h"
#include "wx/osx/dcmemory.h"

#include "wx/osx/private.h"

//-----------------------------------------------------------------------------
// wxMemoryDCImpl
//-----------------------------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(wxMemoryDCImpl, wxPaintDCImpl);


wxMemoryDCImpl::wxMemoryDCImpl( wxMemoryDC *owner )
  : wxPaintDCImpl( owner )
{
    Init();
}

wxMemoryDCImpl::wxMemoryDCImpl( wxMemoryDC *owner, wxBitmap& bitmap )
  : wxPaintDCImpl( owner )
{
    Init();
    DoSelect(bitmap);
}

wxMemoryDCImpl::wxMemoryDCImpl( wxMemoryDC *owner, wxDC * WXUNUSED(dc) )
  : wxPaintDCImpl( owner )
{
    Init();
}

void wxMemoryDCImpl::Init()
{
    m_ok = true;
    wxBrush whiteBrush = wxColour(255, 255, 255);
    SetBackground(whiteBrush);
    SetBrush(whiteBrush);
    wxPen blackPen = wxColour(0, 0, 0);
    SetPen(blackPen);
    SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    m_ok = false;
}

wxMemoryDCImpl::~wxMemoryDCImpl()
{
    if ( m_selected.IsOk() )
    {
        m_selected.SetSelectedInto(NULL);
        wxDELETE(m_graphicContext);
    }
}

void wxMemoryDCImpl::DoSelect( const wxBitmap& bitmap )
{
    if ( m_selected.IsOk() )
    {
        m_selected.SetSelectedInto(NULL);
        wxDELETE(m_graphicContext);
    }

    m_selected = bitmap;
    if (m_selected.IsOk())
    {
        wxASSERT_MSG( !bitmap.GetSelectedInto() ||
                     (bitmap.GetSelectedInto() == GetOwner()),
                     "Bitmap is selected in another wxMemoryDC, delete the first wxMemoryDC or use SelectObject(NULL)" );

        m_selected.SetSelectedInto(GetOwner());
        m_width = bitmap.GetScaledWidth();
        m_height = bitmap.GetScaledHeight();
        m_contentScaleFactor = bitmap.GetScaleFactor();
        CGColorSpaceRef genericColorSpace  = wxMacGetGenericRGBColorSpace();
        CGContextRef bmCtx = (CGContextRef) m_selected.GetHBITMAP();

        if ( bmCtx )
        {
            CGContextSetFillColorSpace( bmCtx, genericColorSpace );
            CGContextSetStrokeColorSpace( bmCtx, genericColorSpace );
            SetGraphicsContext( wxGraphicsContext::CreateFromNative( bmCtx ) );
            if (m_graphicContext)
                m_graphicContext->EnableOffset(m_contentScaleFactor <= 1);
        }
        m_ok = (m_graphicContext != NULL) ;
    }
    else
    {
        m_ok = false;
    }
}

void wxMemoryDCImpl::DoGetSize( int *width, int *height ) const
{
    if (m_selected.IsOk())
    {
        if (width)
            (*width) = m_selected.GetScaledWidth();
        if (height)
            (*height) = m_selected.GetScaledHeight();
    }
    else
    {
        if (width)
            (*width) = 0;
        if (height)
            (*height) = 0;
    }
}
