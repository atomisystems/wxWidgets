/////////////////////////////////////////////////////////////////////////////
// Name:        wx/private/graphics.h
// Purpose:     private graphics context header
// Author:      Stefan Csomor
// Modified by:
// Created:
// Copyright:   (c) Stefan Csomor
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GRAPHICS_PRIVATE_H_
#define _WX_GRAPHICS_PRIVATE_H_

#if wxUSE_GRAPHICS_CONTEXT

#include "wx/graphics.h"

class WXDLLIMPEXP_CORE wxGraphicsObjectRefData : public wxObjectRefData
{
    public :
    wxGraphicsObjectRefData( wxGraphicsRenderer* renderer );
    wxGraphicsObjectRefData( const wxGraphicsObjectRefData* data );
    wxGraphicsRenderer* GetRenderer() const ;
    virtual wxGraphicsObjectRefData* Clone() const ;

    protected :
    wxGraphicsRenderer* m_renderer;
} ;

class WXDLLIMPEXP_CORE wxGraphicsPenData : public wxGraphicsObjectRefData
{
public :
    wxGraphicsPenData( wxGraphicsRenderer* renderer) :
       wxGraphicsObjectRefData(renderer) {}

    virtual ~wxGraphicsPenData() {}
 
    virtual wxDouble GetWidth() const = 0;
} ;

class WXDLLIMPEXP_CORE wxGraphicsBrushData : public wxGraphicsObjectRefData
{
public:
    wxGraphicsBrushData(wxGraphicsRenderer* renderer) :
        wxGraphicsObjectRefData(renderer) {}

    virtual ~wxGraphicsBrushData() {}
    virtual void* GetNativeBrush() const = 0;
    virtual void Transform(const wxGraphicsMatrixData* matrix) = 0;
};

class WXDLLIMPEXP_CORE wxGraphicsBitmapData : public wxGraphicsObjectRefData
{
public :
    wxGraphicsBitmapData( wxGraphicsRenderer* renderer) :
       wxGraphicsObjectRefData(renderer), m_scaleFactor(1.0) {}

    virtual ~wxGraphicsBitmapData() {}

    // returns the native representation
    virtual void * GetNativeBitmap() const = 0;
    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
	int GetScaledWidth() const { return wxRound(GetWidth() / m_scaleFactor); }
	int GetScaledHeight() const  { return wxRound(GetHeight() / m_scaleFactor); }
    double GetScaleFactor() const { return m_scaleFactor; }
	
#ifdef __WXMAC__
	virtual bool HasAlpha() const = 0;
	virtual wxColour GetPixel(int x, int y) = 0;
	
	virtual bool FillBGRA(unsigned char* pDataDest, int nLineSize) = 0;
	virtual bool FillARGB(unsigned char* pDataDest, int nLineSize) = 0;
	virtual bool FillBGR24(unsigned char* pDataDest, int nLineSize, bool bNoAlphaHint = false) = 0;
	
	virtual bool SetBGRA(const unsigned char* pBGRA, int nLineSize) = 0;
	virtual bool SetARGB(const unsigned char* pBGRA, int nLineSize) = 0;
	virtual void MakeTransparent() = 0;
	virtual void ChangeData(void (*fptr)(void* rowData, int w, void* optionData), void* optionData) = 0;
	
	virtual wxGraphicsBitmapData* CreateShadowData( int nBlurRadius, unsigned char r, unsigned char g, unsigned char b, unsigned a ) = 0;
	virtual wxGraphicsBitmapData* CreateBlurredData( int nBlurRadiusH, int nBlurRadiusV) = 0;
	virtual void *BeginRawAccess() = 0;
	virtual void EndRawAccess() = 0;
	virtual CGContextRef GetBitmapContext() const = 0;
	virtual WXImage GetImage() const = 0;
#endif

protected:
       double m_scaleFactor;
} ;

class WXDLLIMPEXP_CORE wxGraphicsMatrixData : public wxGraphicsObjectRefData
{
public :
    wxGraphicsMatrixData( wxGraphicsRenderer* renderer) :
       wxGraphicsObjectRefData(renderer) {}

       virtual ~wxGraphicsMatrixData() {}

       // concatenates the matrix
       virtual void Concat( const wxGraphicsMatrixData *t ) = 0;

       // sets the matrix to the respective values
       virtual void Set(wxDouble a=1.0, wxDouble b=0.0, wxDouble c=0.0, wxDouble d=1.0,
           wxDouble tx=0.0, wxDouble ty=0.0) = 0;

       // gets the component valuess of the matrix
       virtual void Get(wxDouble* a=NULL, wxDouble* b=NULL,  wxDouble* c=NULL,
                        wxDouble* d=NULL, wxDouble* tx=NULL, wxDouble* ty=NULL) const = 0;

       // makes this the inverse matrix
       virtual void Invert() = 0;

       // returns true if the elements of the transformation matrix are equal ?
       virtual bool IsEqual( const wxGraphicsMatrixData* t) const  = 0;

       // return true if this is the identity matrix
       virtual bool IsIdentity() const = 0;

       //
       // transformation
       //

       // add the translation to this matrix
       virtual void Translate( wxDouble dx , wxDouble dy ) = 0;

       // add the scale to this matrix
       virtual void Scale( wxDouble xScale , wxDouble yScale ) = 0;

       // add the rotation to this matrix (radians)
       virtual void Rotate( wxDouble angle ) = 0;

       //
       // apply the transforms
       //

       // applies that matrix to the point
       virtual void TransformPoint( wxDouble *x, wxDouble *y ) const = 0;

       // applies the matrix except for translations
       virtual void TransformDistance( wxDouble *dx, wxDouble *dy ) const =0;

       // returns the native representation
       virtual void * GetNativeMatrix() const = 0;
} ;

class WXDLLIMPEXP_CORE wxGraphicsPathData : public wxGraphicsObjectRefData
{
public :
    wxGraphicsPathData(wxGraphicsRenderer* renderer) : wxGraphicsObjectRefData(renderer) {}
    virtual ~wxGraphicsPathData() {}

    //
    // These are the path primitives from which everything else can be constructed
    //

    // begins a new subpath at (x,y)
    virtual void MoveToPoint( wxDouble x, wxDouble y ) = 0;

    // adds a straight line from the current point to (x,y)
    virtual void AddLineToPoint( wxDouble x, wxDouble y ) = 0;

    // adds a cubic Bezier curve from the current point, using two control points and an end point
    virtual void AddCurveToPoint( wxDouble cx1, wxDouble cy1, wxDouble cx2, wxDouble cy2, wxDouble x, wxDouble y ) = 0;

    // adds another path
    virtual void AddPath( const wxGraphicsPathData* path ) =0;

    // closes the current sub-path
    virtual void CloseSubpath() = 0;

    // gets the last point of the current path, (0,0) if not yet set
    virtual void GetCurrentPoint( wxDouble* x, wxDouble* y) const = 0;

    // adds an arc of a circle centering at (x,y) with radius (r) from startAngle to endAngle
    virtual void AddArc( wxDouble x, wxDouble y, wxDouble r, wxDouble startAngle, wxDouble endAngle, bool clockwise ) = 0;

    //
    // These are convenience functions which - if not available natively will be assembled
    // using the primitives from above
    //

    // adds a quadratic Bezier curve from the current point, using a control point and an end point
    virtual void AddQuadCurveToPoint( wxDouble cx, wxDouble cy, wxDouble x, wxDouble y );

    // appends a rectangle as a new closed subpath
    virtual void AddRectangle( wxDouble x, wxDouble y, wxDouble w, wxDouble h );

    // appends an ellipsis as a new closed subpath fitting the passed rectangle
    virtual void AddCircle( wxDouble x, wxDouble y, wxDouble r );

    // appends a an arc to two tangents connecting (current) to (x1,y1) and (x1,y1) to (x2,y2), also a straight line from (current) to (x1,y1)
    virtual void AddArcToPoint( wxDouble x1, wxDouble y1 , wxDouble x2, wxDouble y2, wxDouble r ) ;

    // appends an ellipse
    virtual void AddEllipse( wxDouble x, wxDouble y, wxDouble w, wxDouble h);

    // appends a rounded rectangle
    virtual void AddRoundedRectangle( wxDouble x, wxDouble y, wxDouble w, wxDouble h, wxDouble radius);

    // returns the native path
    virtual void * GetNativePath() const = 0;

    // give the native path returned by GetNativePath() back (there might be some deallocations necessary)
    virtual void UnGetNativePath(void *p) const= 0;

    // transforms each point of this path by the matrix
    virtual void Transform( const wxGraphicsMatrixData* matrix ) =0;

    // gets the bounding box enclosing all points (possibly including control points)
    virtual void GetBox(wxDouble *x, wxDouble *y, wxDouble *w, wxDouble *h) const=0;

    //gets the bounding box including the width of the pen
    virtual void GetWidenedBox(const wxGraphicsPenData* pen, const wxGraphicsMatrixData* matrix, wxDouble *x, wxDouble *y, wxDouble *w, wxDouble *h) const = 0;

    virtual bool Contains( wxDouble x, wxDouble y, wxPolygonFillMode fillStyle = wxODDEVEN_RULE) const=0;

    virtual bool OutlineContains(wxDouble x, wxDouble y, const wxGraphicsPenData* pen) const = 0;

    virtual void ConvertToStrokePath(const wxGraphicsPenData* pen) = 0;
};

#endif

#endif // _WX_GRAPHICS_PRIVATE_H_
