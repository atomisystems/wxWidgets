/////////////////////////////////////////////////////////////////////////////
// Name:        src/osx/carbon/graphics.cpp
// Purpose:     wxDC class
// Author:      Stefan Csomor
// Modified by:
// Created:     01/02/97
// copyright:   (c) Stefan Csomor
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/graphics.h"
#include "wx/private/graphics.h"

#ifndef WX_PRECOMP
    #include "wx/dcclient.h"
    #include "wx/dcmemory.h"
    #include "wx/dcprint.h"
    #include "wx/log.h"
    #include "wx/region.h"
    #include "wx/image.h"
    #include "wx/icon.h"
#endif


#ifdef __MSL__
    #if __MSL__ >= 0x6000
        #include "math.h"
        // in case our functions were defined outside std, we make it known all the same
        namespace std { }
        using namespace std;
    #endif
#endif

#ifdef __WXMAC__
	#include "wx/settings.h"
    #include "wx/osx/private.h"
    #include "wx/osx/dcprint.h"
    #include "wx/osx/dcclient.h"
    #include "wx/osx/dcmemory.h"
    #include "wx/osx/private.h"
    #include "wx/osx/core/cfdictionary.h"
#else
    #include "CoreServices/CoreServices.h"
    #include "ApplicationServices/ApplicationServices.h"
    #include "wx/osx/core/cfstring.h"
    #include "wx/cocoa/dcclient.h"
#endif

#if wxOSX_USE_COCOA_OR_IPHONE
extern CGContextRef wxOSXGetContextFromCurrentContext() ;
#if wxOSX_USE_COCOA
extern bool wxOSXLockFocus( WXWidget view) ;
extern void wxOSXUnlockFocus( WXWidget view) ;
#endif
#endif

#define wxOSX_USE_PREMULTIPLIED_ALPHA 1

// copying values from NSCompositingModes (see also webkit and cairo sources)

typedef enum CGCompositeOperation {
   kCGCompositeOperationClear           = 0,
   kCGCompositeOperationCopy            = 1,
   kCGCompositeOperationSourceOver      = 2,
   kCGCompositeOperationSourceIn        = 3,
   kCGCompositeOperationSourceOut       = 4,
   kCGCompositeOperationSourceAtop      = 5,
   kCGCompositeOperationDestinationOver = 6,
   kCGCompositeOperationDestinationIn   = 7,
   kCGCompositeOperationDestinationOut  = 8,
   kCGCompositeOperationDestinationAtop = 9,
   kCGCompositeOperationXOR             = 10,
   kCGCompositeOperationPlusDarker      = 11,
// NS only, unsupported by CG : Highlight
   kCGCompositeOperationPlusLighter     = 12
} CGCompositeOperation ;

extern "C"
{
   CG_EXTERN void CGContextSetCompositeOperation (CGContextRef context, int operation);
} ;

//-----------------------------------------------------------------------------
// constants
//-----------------------------------------------------------------------------

#ifndef M_PI
const double M_PI = 3.14159265358979;
#endif

//
// Pen, Brushes and Fonts
//

#pragma mark -
#pragma mark wxMacCoreGraphicsPattern, ImagePattern, HatchPattern classes

OSStatus wxMacDrawCGImage(
                  CGContextRef    inContext,
                  const CGRect *  inBounds,
                  CGImageRef      inImage)
{
    CGContextSaveGState(inContext);
    CGContextTranslateCTM(inContext, inBounds->origin.x, inBounds->origin.y + inBounds->size.height);
    CGRect r = *inBounds;
    r.origin.x = r.origin.y = 0;
    CGContextScaleCTM(inContext, 1, -1);
    CGContextDrawImage(inContext, r, inImage );
    CGContextRestoreGState(inContext);
    return noErr;
}

CGColorRef wxMacCreateCGColor( const wxColour& col )
{
    CGColorRef retval = col.CreateCGColor();

    wxASSERT(retval != NULL);
    return retval;
}

// CGPattern wrapper class: always allocate on heap, never call destructor

class wxMacCoreGraphicsPattern
{
public :
    wxMacCoreGraphicsPattern() {}

    // is guaranteed to be called only with a non-Null CGContextRef
    virtual void Render( CGContextRef ctxRef ) = 0;

    operator CGPatternRef() const { return m_patternRef; }

protected :
    virtual ~wxMacCoreGraphicsPattern()
    {
        // as this is called only when the m_patternRef is been released;
        // don't release it again
    }

    static void _Render( void *info, CGContextRef ctxRef )
    {
        wxMacCoreGraphicsPattern* self = (wxMacCoreGraphicsPattern*) info;
        if ( self && ctxRef )
            self->Render( ctxRef );
    }

    static void _Dispose( void *info )
    {
        wxMacCoreGraphicsPattern* self = (wxMacCoreGraphicsPattern*) info;
        delete self;
    }

    CGPatternRef m_patternRef;

    static const CGPatternCallbacks ms_Callbacks;
};

const CGPatternCallbacks wxMacCoreGraphicsPattern::ms_Callbacks = { 0, &wxMacCoreGraphicsPattern::_Render, &wxMacCoreGraphicsPattern::_Dispose };

class ImagePattern : public wxMacCoreGraphicsPattern
{
public :
    ImagePattern( const wxBitmap* bmp , const CGAffineTransform& transform )
    {
        wxASSERT( bmp && bmp->IsOk() );
#ifdef __WXMAC__
        Init( (CGImageRef) bmp->CreateCGImage() , transform );
#endif
    }

    // ImagePattern takes ownership of CGImageRef passed in
    ImagePattern( CGImageRef image , const CGAffineTransform& transform )
    {
        if ( image )
            CFRetain( image );

        Init( image , transform );
    }

    virtual void Render( CGContextRef ctxRef ) wxOVERRIDE
    {
        if (m_image != NULL)
            wxMacDrawCGImage( ctxRef, &m_imageBounds, m_image );
    }

protected :
    void Init( CGImageRef image, const CGAffineTransform& transform )
    {
        m_image = image;
        if ( m_image )
        {
            m_imageBounds = CGRectMake( (CGFloat) 0.0, (CGFloat) 0.0, (CGFloat)CGImageGetWidth( m_image ), (CGFloat)CGImageGetHeight( m_image ) );
            m_patternRef = CGPatternCreate(
                this , m_imageBounds, transform ,
                m_imageBounds.size.width, m_imageBounds.size.height,
                kCGPatternTilingNoDistortion, true , &wxMacCoreGraphicsPattern::ms_Callbacks );
        }
    }

    virtual ~ImagePattern()
    {
        if ( m_image )
            CGImageRelease( m_image );
    }

    CGImageRef m_image;
    CGRect m_imageBounds;
};

class HatchPattern : public wxMacCoreGraphicsPattern
{
public :
    HatchPattern( int hatchstyle, const CGAffineTransform& transform )
    {
        m_hatch = hatchstyle;
        m_imageBounds = CGRectMake( (CGFloat) 0.0, (CGFloat) 0.0, (CGFloat) 8.0 , (CGFloat) 8.0 );
        m_patternRef = CGPatternCreate(
            this , m_imageBounds, transform ,
            m_imageBounds.size.width, m_imageBounds.size.height,
            kCGPatternTilingNoDistortion, false , &wxMacCoreGraphicsPattern::ms_Callbacks );
    }

    void StrokeLineSegments( CGContextRef ctxRef , const CGPoint pts[] , size_t count )
    {
        CGContextStrokeLineSegments( ctxRef , pts , count );
    }

    virtual void Render( CGContextRef ctxRef ) wxOVERRIDE
    {
        switch ( m_hatch )
        {
            case wxHATCHSTYLE_BDIAGONAL :
                {
                    CGPoint pts[] =
                    {
                    { (CGFloat) 8.0 , (CGFloat) 0.0 } , { (CGFloat) 0.0 , (CGFloat) 8.0 }
                    };
                    StrokeLineSegments( ctxRef , pts , 2 );
                }
                break;

            case wxHATCHSTYLE_CROSSDIAG :
                {
                    CGPoint pts[] =
                    {
                        { (CGFloat) 0.0 , (CGFloat) 0.0 } , { (CGFloat) 8.0 , (CGFloat) 8.0 } ,
                        { (CGFloat) 8.0 , (CGFloat) 0.0 } , { (CGFloat) 0.0 , (CGFloat) 8.0 }
                    };
                    StrokeLineSegments( ctxRef , pts , 4 );
                }
                break;

            case wxHATCHSTYLE_FDIAGONAL :
                {
                    CGPoint pts[] =
                    {
                    { (CGFloat) 0.0 , (CGFloat) 0.0 } , { (CGFloat) 8.0 , (CGFloat) 8.0 }
                    };
                    StrokeLineSegments( ctxRef , pts , 2 );
                }
                break;

            case wxHATCHSTYLE_CROSS :
                {
                    CGPoint pts[] =
                    {
                    { (CGFloat) 0.0 , (CGFloat) 4.0 } , { (CGFloat) 8.0 , (CGFloat) 4.0 } ,
                    { (CGFloat) 4.0 , (CGFloat) 0.0 } , { (CGFloat) 4.0 , (CGFloat) 8.0 } ,
                    };
                    StrokeLineSegments( ctxRef , pts , 4 );
                }
                break;

            case wxHATCHSTYLE_HORIZONTAL :
                {
                    CGPoint pts[] =
                    {
                    { (CGFloat) 0.0 , (CGFloat) 4.0 } , { (CGFloat) 8.0 , (CGFloat) 4.0 } ,
                    };
                    StrokeLineSegments( ctxRef , pts , 2 );
                }
                break;

            case wxHATCHSTYLE_VERTICAL :
                {
                    CGPoint pts[] =
                    {
                    { (CGFloat) 4.0 , (CGFloat) 0.0 } , { (CGFloat) 4.0 , (CGFloat) 8.0 } ,
                    };
                    StrokeLineSegments( ctxRef , pts , 2 );
                }
                break;

            default:
                break;
        }
    }

protected :
    virtual ~HatchPattern() {}

    CGRect      m_imageBounds;
    int         m_hatch;
};

class wxMacCoreGraphicsPenData : public wxGraphicsPenData
{
public:
    wxMacCoreGraphicsPenData( wxGraphicsRenderer* renderer, const wxGraphicsPenInfo& info );
    ~wxMacCoreGraphicsPenData();

    void Init();
    virtual void Apply( wxGraphicsContext* context );
    virtual wxDouble GetWidth() const { return m_width; }
    void SetWidth(wxDouble dWidth) { m_width = dWidth; }
    virtual CGLineCap GetCap() const { return m_cap;}
    virtual CGLineJoin GetJoin() const {return m_join;}
    virtual CGFloat GetCount() const {return m_count;}
    const CGFloat* GetLengths() const {return m_lengths;}

protected :
    CGLineCap m_cap;
    wxCFRef<CGColorRef> m_color;
    wxCFRef<CGColorSpaceRef> m_colorSpace;

    CGLineJoin m_join;
    CGFloat m_width;

    int m_count;
    const CGFloat *m_lengths;
    CGFloat *m_userLengths;


    bool m_isPattern;
    wxCFRef<CGPatternRef> m_pattern;
    CGFloat* m_patternColorComponents;
};

wxMacCoreGraphicsPenData::wxMacCoreGraphicsPenData( wxGraphicsRenderer* renderer,
                                                    const wxGraphicsPenInfo& info )
    : wxGraphicsPenData( renderer )
{
    Init();

    m_color.reset( wxMacCreateCGColor( info.GetColour() ) ) ;

    // TODO: * m_dc->m_scaleX
    m_width = info.GetWidth();
    if (m_width <= 0.0)
        m_width = (CGFloat) 0.1;

    switch ( info.GetCap() )
    {
        case wxCAP_ROUND :
            m_cap = kCGLineCapRound;
            break;

        case wxCAP_PROJECTING :
            m_cap = kCGLineCapSquare;
            break;

        case wxCAP_BUTT :
            m_cap = kCGLineCapButt;
            break;

        default :
            m_cap = kCGLineCapButt;
            break;
    }

    switch ( info.GetJoin() )
    {
        case wxJOIN_BEVEL :
            m_join = kCGLineJoinBevel;
            break;

        case wxJOIN_MITER :
            m_join = kCGLineJoinMiter;
            break;

        case wxJOIN_ROUND :
            m_join = kCGLineJoinRound;
            break;

        default :
            m_join = kCGLineJoinMiter;
            break;
    }

    const CGFloat dashUnit = m_width < 1.0 ? (CGFloat) 1.0 : m_width;

    const CGFloat dotted[] = { (CGFloat) dashUnit , (CGFloat) (dashUnit + 2.0) };
    static const CGFloat short_dashed[] = { (CGFloat) (dashUnit * 2), (CGFloat) (dashUnit * 2) };
    static const CGFloat dashed[] = { (CGFloat) (dashUnit * 3) , (CGFloat) (dashUnit * 3) };
    static const CGFloat dotted_dashed[] = { (CGFloat) (dashUnit * 2) , (CGFloat) (dashUnit * 2) , (CGFloat) (dashUnit * 0) , (CGFloat) (dashUnit * 2) };

    switch ( info.GetStyle() )
    {
        case wxPENSTYLE_SOLID:
            break;

        case wxPENSTYLE_DOT:
            m_count = WXSIZEOF(dotted);
            m_userLengths = new CGFloat[ m_count ] ;
            memcpy( m_userLengths, dotted, sizeof(dotted) );
            m_lengths = m_userLengths;
            break;

        case wxPENSTYLE_LONG_DASH:
            m_count = WXSIZEOF(dashed);
            m_lengths = dashed;
            break;

        case wxPENSTYLE_SHORT_DASH:
            m_count = WXSIZEOF(short_dashed);
            m_lengths = short_dashed;
            break;

        case wxPENSTYLE_DOT_DASH:
            m_count = WXSIZEOF(dotted_dashed);
            m_lengths = dotted_dashed;
            break;

        case wxPENSTYLE_USER_DASH:
            wxDash *dashes;
            m_count = info.GetDashes( &dashes );
            if ((dashes != NULL) && (m_count > 0))
            {
                m_userLengths = new CGFloat[m_count];
                for ( int i = 0; i < m_count; ++i )
                {
                    m_userLengths[i] = dashes[i] * dashUnit;

                    if ( i % 2 == 1 && m_userLengths[i] < dashUnit + 2.0 )
                        m_userLengths[i] = (CGFloat) (dashUnit + 2.0);
                    else if ( i % 2 == 0 && m_userLengths[i] < dashUnit )
                        m_userLengths[i] = dashUnit;
                }
            }
            m_lengths = m_userLengths;
            break;

        case wxPENSTYLE_STIPPLE:
            {
                wxBitmap bmp = info.GetStipple();
                if ( bmp.IsOk() )
                {
                    m_colorSpace.reset( CGColorSpaceCreatePattern( NULL ) );
                    m_pattern.reset( (CGPatternRef) *( new ImagePattern( &bmp , CGAffineTransformMakeScale( 1,-1 ) ) ) );
                    m_patternColorComponents = new CGFloat[1] ;
                    m_patternColorComponents[0] = (CGFloat) 1.0;
                    m_isPattern = true;
                }
            }
            break;

        default :
            {
                m_isPattern = true;
                m_colorSpace.reset( CGColorSpaceCreatePattern( wxMacGetGenericRGBColorSpace() ) );
                m_pattern.reset( (CGPatternRef) *( new HatchPattern( info.GetStyle() , CGAffineTransformMakeScale( 1,-1 ) ) ) );
                m_patternColorComponents = new CGFloat[4] ;
                m_patternColorComponents[0] = (CGFloat) (info.GetColour().Red() / 255.0);
                m_patternColorComponents[1] = (CGFloat) (info.GetColour().Green() / 255.0);
                m_patternColorComponents[2] = (CGFloat) (info.GetColour().Blue() / 255.0);
                m_patternColorComponents[3] =  (CGFloat) (info.GetColour().Alpha() / 255.0);
            }
            break;
    }
    if ((m_lengths != NULL) && (m_count > 0))
    {
        // force the line cap, otherwise we get artifacts (overlaps) and just solid lines
        m_cap = kCGLineCapButt;
    }
}

wxMacCoreGraphicsPenData::~wxMacCoreGraphicsPenData()
{
    delete[] m_userLengths;
    delete[] m_patternColorComponents;
}

void wxMacCoreGraphicsPenData::Init()
{
    m_lengths = NULL;
    m_userLengths = NULL;
    m_width = 0;
    m_count = 0;
    m_patternColorComponents = NULL;
    m_isPattern = false;
}

void wxMacCoreGraphicsPenData::Apply( wxGraphicsContext* context )
{
    CGContextRef cg = (CGContextRef) context->GetNativeContext();
    CGContextSetLineWidth( cg , m_width );
    CGContextSetLineJoin( cg , m_join );

    CGContextSetLineDash( cg , 0 , m_lengths , m_count );
    CGContextSetLineCap( cg , m_cap );

    if ( m_isPattern )
    {
        CGAffineTransform matrix = CGContextGetCTM( cg );
        CGContextSetPatternPhase( cg, CGSizeMake(matrix.tx, matrix.ty) );
        CGContextSetStrokeColorSpace( cg , m_colorSpace );
        CGContextSetStrokePattern( cg, m_pattern , m_patternColorComponents );
    }
    else
    {
        CGContextSetStrokeColorWithColor( cg , m_color );
    }
}

//
// Brush
//

// make sure we all use one class for all conversions from wx to native colour

class wxMacCoreGraphicsColour
{
public:
	wxMacCoreGraphicsColour();
	wxMacCoreGraphicsColour(const wxBrush &brush);
	~wxMacCoreGraphicsColour();

	void SetGraphicsColourMac(CGImageRef image);
	void Apply( CGContextRef cgContext );
	void Transform(CGImageRef image, CGAffineTransform& matrixData );
protected:
	void Init();
	wxCFRef<CGColorRef> m_color;
	wxCFRef<CGColorSpaceRef> m_colorSpace;

	bool m_isPattern;
	wxCFRef<CGPatternRef> m_pattern;
	CGFloat* m_patternColorComponents;
} ;

wxMacCoreGraphicsColour::~wxMacCoreGraphicsColour()
{
    delete[] m_patternColorComponents;
}

void wxMacCoreGraphicsColour::Init()
{
    m_isPattern = false;
    m_patternColorComponents = NULL;
}

void wxMacCoreGraphicsColour::Apply( CGContextRef cgContext )
{
    if ( m_isPattern )
    {
        CGAffineTransform matrix = CGContextGetCTM( cgContext );
        CGContextSetPatternPhase( cgContext, CGSizeMake(matrix.tx, matrix.ty) );
        CGContextSetFillColorSpace( cgContext , m_colorSpace );
        CGContextSetFillPattern( cgContext, m_pattern , m_patternColorComponents );
    }
    else
    {
        CGContextSetFillColorWithColor( cgContext, m_color );
    }
}

wxMacCoreGraphicsColour::wxMacCoreGraphicsColour()
{
    Init();
}

wxMacCoreGraphicsColour::wxMacCoreGraphicsColour( const wxBrush &brush )
{
    Init();
    if ( brush.GetStyle() == wxBRUSHSTYLE_SOLID )
    {
        m_color.reset( wxMacCreateCGColor( brush.GetColour() ));
    }
    else if ( brush.IsHatch() )
    {
        m_isPattern = true;
        m_colorSpace.reset( CGColorSpaceCreatePattern( wxMacGetGenericRGBColorSpace() ) );
        m_pattern.reset( (CGPatternRef) *( new HatchPattern( brush.GetStyle() , CGAffineTransformMakeScale( 1,-1 ) ) ) );

        m_patternColorComponents = new CGFloat[4] ;
        m_patternColorComponents[0] = (CGFloat) (brush.GetColour().Red() / 255.0);
        m_patternColorComponents[1] = (CGFloat) (brush.GetColour().Green() / 255.0);
        m_patternColorComponents[2] = (CGFloat) (brush.GetColour().Blue() / 255.0);
        m_patternColorComponents[3] = (CGFloat) (brush.GetColour().Alpha() / 255.0);
    }
    else
    {
        // now brush is a bitmap
        wxBitmap* bmp = brush.GetStipple();
        if ( bmp && bmp->IsOk() )
        {
            m_isPattern = true;
            m_patternColorComponents = new CGFloat[1] ;
            m_patternColorComponents[0] = (CGFloat) 1.0;
            m_colorSpace.reset( CGColorSpaceCreatePattern( NULL ) );
            m_pattern.reset( (CGPatternRef) *( new ImagePattern( bmp , CGAffineTransformMakeScale( 1,-1 ) ) ) );
        }
    }
}

void wxMacCoreGraphicsColour::SetGraphicsColourMac(CGImageRef image)
{ 
	m_isPattern = true;
	m_patternColorComponents = new CGFloat[1] ;
	m_patternColorComponents[0] = (CGFloat) 1.0;
	m_colorSpace.reset( CGColorSpaceCreatePattern( NULL ) );
	m_pattern.reset( (CGPatternRef) *( new ImagePattern( image , CGAffineTransformMakeScale( 1,-1 ) ) ) );
}

void wxMacCoreGraphicsColour::Transform(CGImageRef image, CGAffineTransform &matrixData)
{ 
	if(m_pattern)
	{
		m_isPattern = true;
		m_patternColorComponents = new CGFloat[1] ;
		m_patternColorComponents[0] = (CGFloat) 1.0;
		m_colorSpace.reset( CGColorSpaceCreatePattern( NULL ) );
		m_pattern.reset( (CGPatternRef) *( new ImagePattern( image , matrixData ) ) );
	}
	else{
	}
}



class wxMacCoreGraphicsBrushData : public wxGraphicsBrushData
{
public:
    wxMacCoreGraphicsBrushData( wxGraphicsRenderer* renderer );
    wxMacCoreGraphicsBrushData( wxGraphicsRenderer* renderer, const wxBrush &brush );
	wxMacCoreGraphicsBrushData( wxGraphicsRenderer* renderer, const wxGraphicsBitmap& bitmap);
    ~wxMacCoreGraphicsBrushData ();

    virtual void Apply( wxGraphicsContext* context );
    void CreateLinearGradientBrush(wxDouble x1, wxDouble y1,
                                   wxDouble x2, wxDouble y2,
                                   const wxGraphicsGradientStops& stops);
    void CreateRadialGradientBrush(wxDouble xo, wxDouble yo,
                                   wxDouble xc, wxDouble yc, wxDouble radius,
                                   const wxGraphicsGradientStops& stops);

    virtual bool IsShading() { return m_isShading; }
    CGShadingRef GetShading() { return m_shading; }
	CGAffineTransform* GetTransform() { return m_transform; }
	bool IsTransform() { return m_isTransform; }
	
	virtual void* GetNativeBrush() const;
	virtual void Transform(const wxGraphicsMatrixData* matrix);
	
	void SetScaleContext(float scalex, float scaley);
	
	bool IsImageFill() { return m_cgImage; }
protected:
    CGFunctionRef CreateGradientFunction(const wxGraphicsGradientStops& stops);

    static void CalculateShadingValues (void *info, const CGFloat *in, CGFloat *out);
    virtual void Init();

    wxMacCoreGraphicsColour m_cgColor;

    bool m_isShading;
	bool m_isTransform;
    CGFunctionRef m_gradientFunction;
    CGShadingRef m_shading;
	CGImageRef m_cgImage;
	CGAffineTransform* m_transform;
	float m_fScaleX;
	float m_fScaleY;

    // information about a single gradient component
    struct GradientComponent
    {
        CGFloat pos;
        CGFloat red;
        CGFloat green;
        CGFloat blue;
        CGFloat alpha;
    };

    // and information about all of them
    struct GradientComponents
    {
        GradientComponents()
        {
            count = 0;
            comps = NULL;
        }

        void Init(unsigned count_)
        {
            count = count_;
            comps = new GradientComponent[count];
        }

        ~GradientComponents()
        {
            delete [] comps;
        }

        unsigned count;
        GradientComponent *comps;
    };

    GradientComponents m_gradientComponents;
};

wxMacCoreGraphicsBrushData::wxMacCoreGraphicsBrushData( wxGraphicsRenderer* renderer) : wxGraphicsBrushData( renderer )
{
    Init();
}

void
wxMacCoreGraphicsBrushData::CreateLinearGradientBrush(wxDouble x1, wxDouble y1,
                                                      wxDouble x2, wxDouble y2,
                                                      const wxGraphicsGradientStops& stops)
{
    m_gradientFunction = CreateGradientFunction(stops);
    m_shading = CGShadingCreateAxial( wxMacGetGenericRGBColorSpace(), CGPointMake((CGFloat) x1, (CGFloat) y1),
                                        CGPointMake((CGFloat) x2,(CGFloat) y2), m_gradientFunction, true, true ) ;
    m_isShading = true ;
}

void
wxMacCoreGraphicsBrushData::CreateRadialGradientBrush(wxDouble xo, wxDouble yo,
                                                      wxDouble xc, wxDouble yc,
                                                      wxDouble radius,
                                                      const wxGraphicsGradientStops& stops)
{
    m_gradientFunction = CreateGradientFunction(stops);
    m_shading = CGShadingCreateRadial( wxMacGetGenericRGBColorSpace(), CGPointMake((CGFloat) xo,(CGFloat) yo), 0,
                                        CGPointMake((CGFloat) xc,(CGFloat) yc), (CGFloat) radius, m_gradientFunction, true, true ) ;
    m_isShading = true ;
}

wxMacCoreGraphicsBrushData::wxMacCoreGraphicsBrushData(wxGraphicsRenderer* renderer, const wxBrush &brush) : wxGraphicsBrushData( renderer ),
    m_cgColor( brush )
{
    Init();

}

wxMacCoreGraphicsBrushData::~wxMacCoreGraphicsBrushData()
{
    if ( m_shading )
        CGShadingRelease(m_shading);

    if( m_gradientFunction )
        CGFunctionRelease(m_gradientFunction);
}

void wxMacCoreGraphicsBrushData::Init()
{
    m_gradientFunction = NULL;
    m_shading = NULL;
    m_isShading = false;
	m_transform = new CGAffineTransform();
	m_isTransform = false;
	m_fScaleX = 1;
	m_fScaleY = 1;
}

void wxMacCoreGraphicsBrushData::Apply( wxGraphicsContext* context )
{
    CGContextRef cg = (CGContextRef) context->GetNativeContext();

    if ( m_isShading )
    {
        // nothing to set as shades are processed by clipping using the path and filling
    }
    else
    {
        m_cgColor.Apply( cg );
    }
}

void wxMacCoreGraphicsBrushData::CalculateShadingValues (void *info, const CGFloat *in, CGFloat *out)
{
    const GradientComponents& stops = *(GradientComponents*) info ;

    CGFloat f = *in;
    if (f <= 0.0)
    {
        // Start
        out[0] = stops.comps[0].red;
        out[1] = stops.comps[0].green;
        out[2] = stops.comps[0].blue;
        out[3] = stops.comps[0].alpha;
    }
    else if (f >= 1.0)
    {
        // end
        out[0] = stops.comps[stops.count - 1].red;
        out[1] = stops.comps[stops.count - 1].green;
        out[2] = stops.comps[stops.count - 1].blue;
        out[3] = stops.comps[stops.count - 1].alpha;
    }
    else
    {
        // Find first component with position greater than f
        unsigned i;
        for ( i = 0; i < stops.count; i++ )
        {
            if (stops.comps[i].pos > f)
                break;
        }

        // Interpolated between stops
        CGFloat diff = (f - stops.comps[i-1].pos);
        CGFloat range = (stops.comps[i].pos - stops.comps[i-1].pos);
        CGFloat fact = diff / range;

        out[0] = stops.comps[i - 1].red + (stops.comps[i].red - stops.comps[i - 1].red) * fact;
        out[1] = stops.comps[i - 1].green + (stops.comps[i].green - stops.comps[i - 1].green) * fact;
        out[2] = stops.comps[i - 1].blue + (stops.comps[i].blue - stops.comps[i - 1].blue) * fact;
        out[3] = stops.comps[i - 1].alpha + (stops.comps[i].alpha - stops.comps[i - 1].alpha) * fact;
    }
}

CGFunctionRef
wxMacCoreGraphicsBrushData::CreateGradientFunction(const wxGraphicsGradientStops& stops)
{

    static const CGFunctionCallbacks callbacks = { 0, &CalculateShadingValues, NULL };
    static const CGFloat input_value_range [2] = { 0, 1 };
    static const CGFloat output_value_ranges [8] = { 0, 1, 0, 1, 0, 1, 0, 1 };

    m_gradientComponents.Init(stops.GetCount());
    for ( unsigned i = 0; i < m_gradientComponents.count; i++ )
    {
        const wxGraphicsGradientStop stop = stops.Item(i);

        m_gradientComponents.comps[i].pos = stop.GetPosition();

        const wxColour col = stop.GetColour();
        m_gradientComponents.comps[i].red = (CGFloat) (col.Red() / 255.0);
        m_gradientComponents.comps[i].green = (CGFloat) (col.Green() / 255.0);
        m_gradientComponents.comps[i].blue = (CGFloat) (col.Blue() / 255.0);
        m_gradientComponents.comps[i].alpha = (CGFloat) (col.Alpha() / 255.0);
    }

    return CGFunctionCreate ( &m_gradientComponents,  1,
                            input_value_range,
                            4,
                            output_value_ranges,
                            &callbacks);
}

wxMacCoreGraphicsBrushData::wxMacCoreGraphicsBrushData(wxGraphicsRenderer *renderer, const wxGraphicsBitmap& bitmap) : wxGraphicsBrushData( renderer )
{ 
	Init();
	m_cgImage = (CGImageRef)bitmap.GetNativeBitmap();
	m_cgColor.SetGraphicsColourMac(m_cgImage);
}


void wxMacCoreGraphicsBrushData::Transform(const wxGraphicsMatrixData* matrix)
{ 
	CGAffineTransform* transform = (CGAffineTransform*)matrix->GetNativeMatrix();
	m_transform->a = transform->a;
	m_transform->b = transform->b;
	m_transform->c = transform->c;
	m_transform->d = transform->d;
	m_transform->tx = transform->tx;
	m_transform->ty = transform->ty;
	if(m_gradientFunction == NULL)
	{
		m_cgColor.Transform(m_cgImage, *m_transform);
		m_isTransform = false;
	}
	else
	{
		m_isTransform = true;
	}
}

void wxMacCoreGraphicsBrushData::SetScaleContext(float scalex, float scaley)
{ 
	if(m_fScaleX == scalex && m_fScaleY == scaley && m_cgImage)
		return;
	m_fScaleX = scalex;
	m_fScaleY = scaley;
	CGAffineTransform transform = CGAffineTransformMake(1.0, 0.0, 0.0, 1.0, 0.0, 0.0);
	transform = CGAffineTransformScale( transform , scalex , scalex);
	transform = CGAffineTransformTranslate( transform , m_transform->tx , m_transform->ty);
	transform = CGAffineTransformScale( transform , m_transform->a , m_transform->d);
	if(m_gradientFunction == NULL)
	{
		m_cgColor.Transform(m_cgImage, transform);
		m_isTransform = false;
	}
	else
	{
		m_isTransform = true;
	}
}

void *wxMacCoreGraphicsBrushData::GetNativeBrush() const
{ 
	return nullptr;
}
`

//
// Font
//

class wxMacCoreGraphicsFontData : public wxGraphicsObjectRefData
{
public:
    wxMacCoreGraphicsFontData( wxGraphicsRenderer* renderer, const wxFont &font, const wxColour& col );
    ~wxMacCoreGraphicsFontData();

    CTFontRef OSXGetCTFont() const { return m_ctFont ; }
    CFDictionaryRef OSXGetCTFontAttributes() const { return m_ctFontAttributes; }
    wxColour GetColour() const { return m_colour ; }

    bool GetUnderlined() const { return m_underlined ; }
    bool GetStrikethrough() const { return m_strikethrough; }

#if wxOSX_USE_IPHONE
    UIFont* GetUIFont() const { return m_uiFont; }
#endif
private :
    wxColour m_colour;
    bool m_underlined,
         m_strikethrough;
    wxCFRef< CTFontRef > m_ctFont;
    wxCFRef< CFDictionaryRef > m_ctFontAttributes;
#if wxOSX_USE_IPHONE
    wxCFRef< WX_UIFont > m_uiFont;
#endif
};

wxMacCoreGraphicsFontData::wxMacCoreGraphicsFontData(wxGraphicsRenderer* renderer, const wxFont &font, const wxColour& col) : wxGraphicsObjectRefData( renderer )
    , m_colour(col)
{
    m_underlined = font.GetUnderlined();
    m_strikethrough = font.GetStrikethrough();

    m_ctFont = wxCFRetain(font.OSXGetCTFont());
    m_ctFontAttributes = wxCFRetain(font.OSXGetCTFontAttributes());
#if wxOSX_USE_IPHONE
    m_uiFont = wxCFRetain(font.OSXGetUIFont());
#endif
}

wxMacCoreGraphicsFontData::~wxMacCoreGraphicsFontData()
{
}

class wxMacCoreGraphicsBitmapData : public wxGraphicsBitmapData
{
public:
	wxMacCoreGraphicsBitmapData(wxGraphicsRenderer* renderer);
	wxMacCoreGraphicsBitmapData(wxGraphicsRenderer* renderer, CGImageRef bitmap, double dScaleFactor = 1.0);
	wxMacCoreGraphicsBitmapData(wxGraphicsRenderer* renderer, const wxBitmap &bmp );
	wxMacCoreGraphicsBitmapData(wxGraphicsRenderer* renderer, int w, int h, double dScaleFactor = 1.0);
	wxMacCoreGraphicsBitmapData(wxGraphicsRenderer* renderer, const wxImage &img, double dScaleFactor = 1.0);
	wxMacCoreGraphicsBitmapData(wxGraphicsRenderer* renderer, const unsigned char* pBGRA, int w, int h, double dScaleFactor = 1.0);
	wxMacCoreGraphicsBitmapData(wxGraphicsRenderer* renderer, const wxColour& colour, int w, int h, double dScaleFactor = 1.0);
	wxMacCoreGraphicsBitmapData(wxGraphicsRenderer* renderer, const wxMacCoreGraphicsBitmapData &tocopy);
	
	virtual ~wxMacCoreGraphicsBitmapData ();
	
	virtual bool IsOk() const;
	
	void Free();
	
	int GetWidth() const;
	int GetHeight() const;
	int GetDepth() const;
	int GetBytesPerRow() const;
	int GetScaledWidth() const;
	int GetScaledHeight() const;
	double GetScaleFactor() const { return m_scaleFactor; }
	
	const void *GetRawAccess() const;
	void *GetRawAccess();
	void *BeginRawAccess();
	void EndRawAccess();
	
	void UseAlpha( bool useAlpha );
	
	bool IsTemplate() const { return m_isTemplate; }
	void SetTemplate(bool is) { m_isTemplate = is; }
	
	bool HasAlpha() const;
	wxColour GetPixel(int x, int y);
	wxImage ConvertToImage( bool bRemoveAlpha = false) const;
	
	bool FillBGRA(unsigned char* pDataDest, int nLineSize);
	bool FillARGB(unsigned char* pDataDest, int nLineSize);
	bool FillBGR24(unsigned char* pDataDest, int nLineSize, bool bNoAlphaHint = false);
	
	bool SetBGRA(const unsigned char* pBGRA, int nLineSize);
	bool SetARGB(const unsigned char* pBGRA, int nLineSize);
	void MakeTransparent();
	void ChangeData(void (*fptr)(void* rowData, int w, void* optionData), void* optionData);
	
	wxMacCoreGraphicsBitmapData* CreateShadowData( int nBlurRadius, unsigned char r, unsigned char g, unsigned char b, unsigned a );
	wxMacCoreGraphicsBitmapData* CreateBlurredData( int nBlurRadiusH, int nBlurRadiusV);
	
	virtual void* GetNativeBitmap() const {return CreateCGImage();}
	virtual WXImage GetImage() const;
	
	
public:
#if wxUSE_PALETTE
	wxPalette     m_bitmapPalette;
#endif // wxUSE_PALETTE
	
	CGImageRef    CreateCGImage() const;
	
	// returns true if the bitmap has a size that
	// can be natively transferred into a true icon
	// if no is returned GetIconRef will still produce
	// an icon but it will be generated via a PICT and
	// rescaled to 16 x 16
	bool          HasNativeSize();
	
#if wxOSX_USE_ICONREF
	// caller should increase ref count if needed longer
	// than the bitmap exists
	IconRef       GetIconRef();
#endif
	
	CGContextRef  GetBitmapContext() const;
	
	void SetSelectedInto(wxDC *dc);
	wxDC *GetSelectedInto() const;
	
private:
	bool Create( CGImageRef image, double scale );
	bool Create( int w , int h , int d, double logicalScale );
	bool Create( CGContextRef context);
	bool Create( WXImage image);
	void Init();
	
	void EnsureBitmapExists() const;
	
	void FreeDerivedRepresentations();
	
	WXImage    m_nsImage;
	int           m_rawAccessCount;
	mutable CGImageRef    m_cgImageRef;
	bool          m_isTemplate;
	
#ifndef __WXOSX_IPHONE__
	IconRef       m_iconRef;
#endif
	
	wxCFRef<CGContextRef>  m_hBitmap;
	double		m_scaleFactor;
	wxDC*         m_selectedInto;
};

static const int kBestByteAlignement = 16;
static const int kMaskBytesPerPixel = 1;

static int GetBestBytesPerRow( int rawBytes )
{
	return (((rawBytes)+kBestByteAlignement-1) & ~(kBestByteAlignement-1) );
}

wxMacCoreGraphicsBitmapData::wxMacCoreGraphicsBitmapData(wxGraphicsRenderer* renderer) : wxGraphicsBitmapData(renderer)
{
	Init() ;
}

wxMacCoreGraphicsBitmapData::wxMacCoreGraphicsBitmapData(wxGraphicsRenderer* renderer, const wxBitmap &bmp) : wxGraphicsBitmapData(renderer)
{
	Init();
	if (bmp.IsOk())
	{
		CGImageRef bitmap = bmp.CreateCGImage();
		Create(bitmap, bmp.GetScaleFactor());
		CGImageRelease(bitmap);
	}
}

wxMacCoreGraphicsBitmapData::wxMacCoreGraphicsBitmapData(wxGraphicsRenderer* renderer, int w, int h, double dScaleFactor) : wxGraphicsBitmapData(renderer)
{
	Init() ;
	Create(w , h, 32, dScaleFactor) ;
}


wxMacCoreGraphicsBitmapData::wxMacCoreGraphicsBitmapData(wxGraphicsRenderer* renderer, CGImageRef bitmap, double dScaleFactor) : wxGraphicsBitmapData(renderer)
{
	Init();
	Create(bitmap, dScaleFactor);
}

bool wxMacCoreGraphicsBitmapData::Create(int w , int h , int d, double dScaleFactor)
{
	size_t m_width = wxMax(1, w);
	size_t m_height = wxMax(1, h);
	
	m_scaleFactor = dScaleFactor;
	m_hBitmap = NULL;
	
	size_t m_bytesPerRow = GetBestBytesPerRow(m_width * 4);
	void* data = NULL;
	m_hBitmap = CGBitmapContextCreate((char*)data, m_width, m_height, 8, m_bytesPerRow, wxMacGetGenericRGBColorSpace(), kCGImageAlphaPremultipliedFirst);
	wxASSERT_MSG(m_hBitmap, wxT("Unable to create CGBitmapContext context"));
	CGContextTranslateCTM(m_hBitmap, 0, m_height);
	CGContextScaleCTM(m_hBitmap, 1 * GetScaleFactor(), -1 * GetScaleFactor());
	
	return IsOk();
}

bool wxMacCoreGraphicsBitmapData::Create( CGContextRef context)
{
	if ( context != NULL && CGBitmapContextGetData(context) )
	{
		m_hBitmap = context;
		// our own contexts conform to this, always.
		wxASSERT( GetDepth() == 32 );
		
		// determine content scale
		CGRect userrect = CGRectMake(0, 0, 10, 10);
		CGRect devicerect;
		devicerect = CGContextConvertRectToDeviceSpace(context, userrect);
		m_scaleFactor = devicerect.size.height / userrect.size.height;
	}
	return IsOk() ;
}

bool wxMacCoreGraphicsBitmapData::Create( WXImage image)
{
	m_nsImage = image;
	
	wxMacCocoaRetain(image);
	
	m_scaleFactor = wxOSXGetImageScaleFactor(image);
	
	return true;
}

void wxMacCoreGraphicsBitmapData::EnsureBitmapExists() const
{
	if ( ! m_hBitmap && m_nsImage)
	{
		wxMacCoreGraphicsBitmapData* t =  const_cast<wxMacCoreGraphicsBitmapData*>(this);
		t->m_hBitmap = wxOSXCreateBitmapContextFromImage(m_nsImage, &t->m_isTemplate);
	}
}

wxMacCoreGraphicsBitmapData::wxMacCoreGraphicsBitmapData(wxGraphicsRenderer* renderer, const wxImage &img, double dScaleFactor) : wxGraphicsBitmapData(renderer)
{
	wxCHECK_RET( img.IsOk(), wxT("invalid image") );
	
	// width and height of the device-dependent bitmap
	int width = img.GetWidth();
	int height = img.GetHeight();
	
	Init() ;
	Create(width , height, 32, dScaleFactor) ;
	
	if ( IsOk())
	{
		// Create picture
		
		bool hasAlpha = false ;
		
		if ( img.HasMask() )
		{
			// takes precedence, don't mix with alpha info
		}
		else
		{
			hasAlpha = img.HasAlpha() ;
		}
		
		if ( hasAlpha )
			UseAlpha(true) ;
		
		unsigned char* destinationstart = (unsigned char*) BeginRawAccess() ;
		unsigned char* data = img.GetData();
		if ( destinationstart != NULL && data != NULL )
		{
			const unsigned char *alpha = hasAlpha ? img.GetAlpha() : NULL ;
			for (int y = 0; y < height; destinationstart += GetBytesPerRow(), y++)
			{
				unsigned char * destination = destinationstart;
				for (int x = 0; x < width; x++)
				{
					if ( hasAlpha )
					{
						const unsigned char a = *alpha++;
						*destination++ = a ;
						
#if wxOSX_USE_PREMULTIPLIED_ALPHA
						*destination++ = ((*data++) * a + 127) / 255 ;
						*destination++ = ((*data++) * a + 127) / 255 ;
						*destination++ = ((*data++) * a + 127) / 255 ;
#else
						*destination++ = *data++ ;
						*destination++ = *data++ ;
						*destination++ = *data++ ;
#endif
					}
					else
					{
						*destination++ = 0xFF ;
						*destination++ = *data++ ;
						*destination++ = *data++ ;
						*destination++ = *data++ ;
					}
				}
			}
			
			EndRawAccess() ;
		}
	}
}

wxMacCoreGraphicsBitmapData::wxMacCoreGraphicsBitmapData(wxGraphicsRenderer* renderer, const unsigned char* bgraSrc, int w, int h, double dScaleFactor) : wxGraphicsBitmapData(renderer)
{
	Init();
	Create(w, h, 32, dScaleFactor);
	if(IsOk())
	{
		int linesize = w / 8;
		if ( w % 8 )
			linesize++;
		
		unsigned char* destptr = (unsigned char*) BeginRawAccess() ;
		w = w << 2;
		unsigned char* rowData;
		for (int row = 0; row < h; ++row)
		{
			rowData =(unsigned char *)destptr + (row * GetBytesPerRow());
			memcpy(rowData, bgraSrc, w);
			bgraSrc += w;
		}
		
		EndRawAccess() ;
	}
}

wxMacCoreGraphicsBitmapData::wxMacCoreGraphicsBitmapData(wxGraphicsRenderer* renderer, const wxColour& colour, int w, int h, double dScaleFactor) : wxGraphicsBitmapData(renderer)
{
	
	Init();
	Create(w, h, 32, dScaleFactor);
	CGContextSetRGBFillColor(m_hBitmap, colour.Red(), colour.Green(), colour.Blue(), colour.Alpha());
}

wxMacCoreGraphicsBitmapData::wxMacCoreGraphicsBitmapData(wxGraphicsRenderer* renderer, const wxMacCoreGraphicsBitmapData &tocopy) : wxGraphicsBitmapData(renderer)
{
	Init();
	Create(tocopy.GetWidth(), tocopy.GetHeight(), tocopy.GetDepth(), tocopy.GetScaleFactor());
	
	if (tocopy.HasAlpha())
		UseAlpha(true);
	
	unsigned char* dest = (unsigned char*)GetRawAccess();
	unsigned char* source = (unsigned char*)tocopy.GetRawAccess();
	size_t numbytes = GetBytesPerRow() * GetHeight();
	memcpy( dest, source, numbytes );
}

bool wxMacCoreGraphicsBitmapData::IsOk() const
{
	return (m_hBitmap.get() != NULL || m_nsImage != NULL);
}

int wxMacCoreGraphicsBitmapData::GetWidth() const
{
	wxCHECK_MSG( IsOk() , 0 , "Invalid Bitmap");
	
	if ( m_hBitmap )
		return (int) CGBitmapContextGetWidth(m_hBitmap);
	else
		return (int) wxOSXGetImageSize(m_nsImage).width * m_scaleFactor;
}

int wxMacCoreGraphicsBitmapData::GetHeight() const
{
	wxCHECK_MSG( IsOk() , 0 , "Invalid Bitmap");
	
	if ( m_hBitmap )
		return (int) CGBitmapContextGetHeight(m_hBitmap);
	else
		return (int) wxOSXGetImageSize(m_nsImage).height * m_scaleFactor;
}

int wxMacCoreGraphicsBitmapData::GetDepth() const
{
	wxCHECK_MSG( IsOk() , 0 , "Invalid Bitmap");
	
	if ( m_hBitmap )
		return (int) CGBitmapContextGetBitsPerPixel(m_hBitmap);
	else
		return 32; // a bitmap converted from an nsimage would have this depth
}
int wxMacCoreGraphicsBitmapData::GetBytesPerRow() const
{
	wxCHECK_MSG( IsOk() , 0 , "Invalid Bitmap");
	
	if ( m_hBitmap )
		return (int) CGBitmapContextGetBytesPerRow(m_hBitmap);
	else
		return (int) GetBestBytesPerRow( GetWidth() * 4);
}

bool wxMacCoreGraphicsBitmapData::HasAlpha() const
{
	wxCHECK_MSG( IsOk() , 0 , "Invalid Bitmap");
	
	if ( m_hBitmap )
	{
		CGImageAlphaInfo alpha = CGBitmapContextGetAlphaInfo(m_hBitmap);
		return !(alpha == kCGImageAlphaNone || alpha == kCGImageAlphaNoneSkipFirst || alpha == kCGImageAlphaNoneSkipLast);
	}
	else
	{
		return true; // a bitmap converted from an nsimage would have alpha
	}
}

int wxMacCoreGraphicsBitmapData::GetScaledWidth() const
{
	return wxRound(GetWidth() * m_scaleFactor);
}


int wxMacCoreGraphicsBitmapData::GetScaledHeight() const
{
	return wxRound(GetHeight() * m_scaleFactor);
}

const void *wxMacCoreGraphicsBitmapData::GetRawAccess() const
{
	wxCHECK_MSG( IsOk(), NULL , wxT("invalid bitmap") ) ;
	
	EnsureBitmapExists();
	
	return CGBitmapContextGetData(m_hBitmap);
}

void *wxMacCoreGraphicsBitmapData::GetRawAccess()
{
	return const_cast<void*>(const_cast<const wxMacCoreGraphicsBitmapData*>(this)->GetRawAccess());
}


void *wxMacCoreGraphicsBitmapData::BeginRawAccess()
{
	wxCHECK_MSG( IsOk(), NULL, wxT("invalid bitmap") ) ;
	wxASSERT( m_rawAccessCount == 0 ) ;
	
#if wxOSX_USE_ICONREF
	wxASSERT_MSG( m_iconRef == NULL ,
				 wxT("Currently, modifing bitmaps that are used in controls already is not supported") ) ;
#endif
	
	++m_rawAccessCount ;
	
	// we must destroy an existing cached image, as
	// the bitmap data may change now
	FreeDerivedRepresentations();
	
	return GetRawAccess() ;
}

void wxMacCoreGraphicsBitmapData::EndRawAccess()
{
	wxCHECK_RET( IsOk() , wxT("invalid bitmap") ) ;
	wxASSERT( m_rawAccessCount == 1 ) ;
	
	--m_rawAccessCount ;
}

void wxMacCoreGraphicsBitmapData::UseAlpha( bool use )
{
	wxCHECK_RET( IsOk() , wxT("invalid bitmap") ) ;
	
	if ( HasAlpha() == use )
		return ;
	
	CGContextRef hBitmap = CGBitmapContextCreate(NULL, GetWidth(), GetHeight(), 8, GetBytesPerRow(), wxMacGetGenericRGBColorSpace(), use ? kCGImageAlphaPremultipliedFirst : kCGImageAlphaNoneSkipFirst );
	
	memcpy(CGBitmapContextGetData(hBitmap),CGBitmapContextGetData(m_hBitmap),GetBytesPerRow()*GetHeight());
	
	wxASSERT_MSG( hBitmap , wxT("Unable to create CGBitmapContext context") ) ;
	CGContextTranslateCTM( hBitmap, 0,  GetHeight() );
	CGContextScaleCTM( hBitmap, GetScaleFactor(), -GetScaleFactor() );
	
	m_hBitmap.reset(hBitmap);
}

CGImageRef wxMacCoreGraphicsBitmapData::CreateCGImage() const
{
	wxASSERT( IsOk() ) ;
	wxASSERT( m_rawAccessCount >= 0 ) ;
	CGImageRef image ;
	if ( m_rawAccessCount > 0 || m_cgImageRef == NULL )
	{
		if (m_nsImage)
		{
			image = wxOSXCreateCGImageFromImage(m_nsImage);
		}
		else
		{
			if (GetDepth() != 1)
			{
				image = CGBitmapContextCreateImage(m_hBitmap);
			}
			else
			{
				size_t imageSize = GetHeight() * GetBytesPerRow();
				
				int w = GetWidth();
				int h = GetHeight();
				CGImageAlphaInfo alphaInfo = kCGImageAlphaNoneSkipFirst;
				wxMemoryBuffer membuf;
				
				if (HasAlpha())
				{
#if wxOSX_USE_PREMULTIPLIED_ALPHA
					alphaInfo = kCGImageAlphaPremultipliedFirst;
#else
					alphaInfo = kCGImageAlphaFirst;
#endif
				}
				memcpy(membuf.GetWriteBuf(imageSize), GetRawAccess(), imageSize);
				membuf.UngetWriteBuf(imageSize);
				
				CGDataProviderRef dataProvider = NULL;
				if (GetDepth() == 1)
				{
					// TODO CHECK ALIGNMENT
					wxMemoryBuffer maskBuf;
					unsigned char* maskBufData = (unsigned char*)maskBuf.GetWriteBuf(GetWidth() * GetHeight());
					unsigned char* bufData = (unsigned char*)membuf.GetData();
					// copy one color component
					size_t i = 0;
					for (int y = 0; y < GetHeight(); bufData += GetBytesPerRow(), ++y)
					{
						unsigned char* bufDataIter = bufData + 3;
						for (int x = 0; x < GetWidth(); bufDataIter += 4, ++x, ++i)
						{
							maskBufData[i] = *bufDataIter;
						}
					}
					maskBuf.UngetWriteBuf(GetWidth() * GetHeight());
					
					dataProvider = wxMacCGDataProviderCreateWithMemoryBuffer(maskBuf);
					
					image = ::CGImageMaskCreate(w, h, 8, 8, GetWidth(), dataProvider, NULL, false);
				}
				else
				{
					CGColorSpaceRef colorSpace = wxMacGetGenericRGBColorSpace();
					dataProvider = wxMacCGDataProviderCreateWithMemoryBuffer(membuf);
					image = ::CGImageCreate(
											w, h, 8, 32, GetBytesPerRow(), colorSpace, alphaInfo,
											dataProvider, NULL, false, kCGRenderingIntentDefault);
				}
				CGDataProviderRelease(dataProvider);
			}
		}
	}
	else
	{
		image = m_cgImageRef ;
		CGImageRetain( image ) ;
	}
	
	if ( m_rawAccessCount == 0 && m_cgImageRef == NULL)
	{
		// we keep it for later use
		m_cgImageRef = image ;
		CGImageRetain( image ) ;
	}
	
	return image ;
}

bool wxMacCoreGraphicsBitmapData::HasNativeSize()
{
	int w = GetWidth() ;
	int h = GetHeight() ;
	int sz = wxMax( w , h ) ;
	
	return ( sz == 128 || sz == 48 || sz == 32 || sz == 16 );
}

CGContextRef wxMacCoreGraphicsBitmapData::GetBitmapContext() const
{
	return m_hBitmap;
}

void wxMacCoreGraphicsBitmapData::SetSelectedInto(wxDC *dc)
{
	if ( dc == NULL )
	{
		if ( m_selectedInto != NULL )
			EndRawAccess();
	}
	else
	{
		wxASSERT_MSG( m_selectedInto == NULL || m_selectedInto == dc, "Bitmap already selected into a different dc");
		
		if ( m_selectedInto == NULL )
			(void) BeginRawAccess();
	}
	
	m_selectedInto = dc;
}

wxDC *wxMacCoreGraphicsBitmapData::GetSelectedInto() const
{
	return m_selectedInto;
}

void wxMacCoreGraphicsBitmapData::FreeDerivedRepresentations()
{
	if ( m_cgImageRef )
	{
		CGImageRelease( m_cgImageRef ) ;
		m_cgImageRef = NULL ;
	}
#if wxOSX_USE_ICONREF
	if ( m_iconRef )
	{
		ReleaseIconRef( m_iconRef ) ;
		m_iconRef = NULL ;
	}
#endif // wxOSX_USE_ICONREF
}

void wxMacCoreGraphicsBitmapData::Free()
{
	wxASSERT_MSG( m_rawAccessCount == 0 , wxT("Bitmap still selected when destroyed") ) ;
	
	FreeDerivedRepresentations();
	
	wxMacCocoaRelease(m_nsImage);
	
	m_hBitmap.reset();
}

bool wxMacCoreGraphicsBitmapData::Create(CGImageRef image, double scale)
{
	if ( image != NULL )
	{
		size_t m_width = (int)CGImageGetWidth(image);
		size_t m_height = (int)CGImageGetHeight(image);
		m_hBitmap = NULL;
		m_scaleFactor = scale;
		
		size_t m_bytesPerRow = GetBestBytesPerRow( m_width * 4 ) ;
		void* data = NULL;
		
		CGImageAlphaInfo alpha = CGImageGetAlphaInfo(image);
		if (alpha == kCGImageAlphaNone || alpha == kCGImageAlphaNoneSkipFirst || alpha == kCGImageAlphaNoneSkipLast)
		{
			m_hBitmap = CGBitmapContextCreate((char*)data, m_width, m_height, 8, m_bytesPerRow, wxMacGetGenericRGBColorSpace(), kCGImageAlphaNoneSkipFirst);
		}
		else
		{
			m_hBitmap = CGBitmapContextCreate((char*)data, m_width, m_height, 8, m_bytesPerRow, wxMacGetGenericRGBColorSpace(), kCGImageAlphaPremultipliedFirst);
		}
		CGRect rect = CGRectMake(0, 0, m_width, m_height);
		CGContextDrawImage(m_hBitmap, rect, image);
		
		wxASSERT_MSG(m_hBitmap, wxT("Unable to create CGBitmapContext context"));
		CGContextTranslateCTM(m_hBitmap, 0, m_height);
		CGContextScaleCTM(m_hBitmap, 1 * m_scaleFactor, -1 * m_scaleFactor);
	}
	
	return IsOk() ;
	
}

void wxMacCoreGraphicsBitmapData::Init()
{
	m_nsImage = NULL;
	m_cgImageRef = NULL ;
	m_isTemplate = false;
	
	m_hBitmap = NULL ;
	
	m_rawAccessCount = 0 ;
	m_scaleFactor = 1.0;
	m_selectedInto = NULL;
}

wxMacCoreGraphicsBitmapData::~wxMacCoreGraphicsBitmapData()
{
	Free();
}

wxImage wxMacCoreGraphicsBitmapData::ConvertToImage(bool bRemoveAlpha /*= false*/) const
{
	wxImage image;
	
	wxCHECK_MSG( IsOk(), wxNullImage, wxT("invalid bitmap") );
	
	// this call may trigger a conversion from platform image to bitmap, issue it
	// before any measurements are taken, multi-resolution platform images may be
	// rendered incorrectly otherwise
	unsigned char* sourcestart = (unsigned char* )GetRawAccess() ;
	
	// create an wxImage object
	int width = GetWidth();
	int height = GetHeight();
	image.Create( width, height );
	
	unsigned char *data = image.GetData();
	wxCHECK_MSG( data, wxNullImage, wxT("Could not allocate data for image") );
	
	bool hasAlpha = false ;
	unsigned char *alpha = NULL ;
	
	if ( HasAlpha() )
		hasAlpha = true ;
	
	if ( hasAlpha )
	{
		image.SetAlpha() ;
		alpha = image.GetAlpha() ;
	}
	
	int index = 0;
	
	
	for (int yy = 0; yy < height; yy++ , sourcestart += GetBytesPerRow())
	{
		const wxUint32 * source = (wxUint32*)sourcestart;
		unsigned char a, r, g, b;
		
		for (int xx = 0; xx < width; xx++)
		{
			const wxUint32 color = *source++;
#ifdef WORDS_BIGENDIAN
			a = ((color&0xFF000000) >> 24) ;
			r = ((color&0x00FF0000) >> 16) ;
			g = ((color&0x0000FF00) >> 8) ;
			b = (color&0x000000FF);
#else
			b = ((color&0xFF000000) >> 24) ;
			g = ((color&0x00FF0000) >> 16) ;
			r = ((color&0x0000FF00) >> 8) ;
			a = (color&0x000000FF);
#endif
			if ( hasAlpha )
			{
				*alpha++ = a ;
#if wxOSX_USE_PREMULTIPLIED_ALPHA
				// this must be non-premultiplied data
				if ( a != 0xFF && a!= 0 )
				{
					r = r * 255 / a;
					g = g * 255 / a;
					b = b * 255 / a;
				}
#endif
			}
			
			data[index    ] = r ;
			data[index + 1] = g ;
			data[index + 2] = b ;
			
			index += 3;
		}
	}
	
	return image;
}

wxColour wxMacCoreGraphicsBitmapData::GetPixel( int x, int y )
{
	uint8_t* nBuffer = (uint8_t*)BeginRawAccess();
	int pixelInfo = ((GetWidth()  * y) + x ) * 4;
	wxColour color(nBuffer[pixelInfo+1], nBuffer[pixelInfo+2], nBuffer[pixelInfo+3], nBuffer[pixelInfo]);
	EndRawAccess();
	return color;
}

bool wxMacCoreGraphicsBitmapData::FillBGRA( unsigned char* pDataDest, int nLineSize )
{
	unsigned char* rowData;
	unsigned char* bgrDest;
	uint8_t *nBuffer = (uint8_t *)GetRawAccess();
	if (!nBuffer)
		return false;
	size_t m_bytesPerRow = GetBytesPerRow();
	for (int y = 0; y < GetHeight(); y++)
	{
		rowData = (unsigned char *)(nBuffer + y * m_bytesPerRow);
		bgrDest = (unsigned char *)(pDataDest + y * nLineSize);
		for (int x = 0; x < GetWidth(); x++)
		{
			bgrDest[0] = rowData[3];
			bgrDest[1] = rowData[2];
			bgrDest[2] = rowData[1];
			bgrDest[3] = rowData[0];
			rowData += 4;
			bgrDest += 4;
		}
	}
	return true;
}

bool wxMacCoreGraphicsBitmapData::FillARGB( unsigned char* pDataDest, int nLineSize )
{
	unsigned char* rowData;
	unsigned char* bgrDest;
	uint8_t *nBuffer = (uint8_t *)GetRawAccess();
	if (!nBuffer)
		return false;
	size_t m_bytesPerRow = GetBytesPerRow();
	size_t m_width = GetWidth();
	for (int y = 0; y < GetHeight(); y++)
	{
		rowData = (unsigned char *)(nBuffer + y * m_bytesPerRow);
		bgrDest = (unsigned char *)(pDataDest + y * nLineSize);
		memcpy(bgrDest, rowData, m_width * 4);
	}
	return true;
}


bool wxMacCoreGraphicsBitmapData::FillBGR24(unsigned char* pDataDest, int nLineSize, bool bNoAlphaHint /*= false*/)
{
	unsigned char* rowData;
	unsigned char* bgr24Dest;
	uint8_t *nBuffer = (uint8_t *)GetRawAccess();
	if (!nBuffer)
		return false;
	size_t m_bytesPerRow = GetBytesPerRow();
	if (HasAlpha() && !bNoAlphaHint)
	{
		for (int y = 0; y < GetHeight(); y++)
		{
			rowData =(unsigned char *)(nBuffer + y * m_bytesPerRow);
			bgr24Dest = (unsigned char *)(pDataDest + y * nLineSize);
			for (int x = 0; x < GetWidth(); x++)
			{
				if (rowData[0]>0)
				{
					bgr24Dest[0] = wxClip(rowData[3] * 255/rowData[0], 0, 255);
					bgr24Dest[1] = wxClip(rowData[2] * 255/rowData[0], 0, 255);
					bgr24Dest[2] = wxClip(rowData[1] * 255/rowData[0], 0, 255);
				}
				rowData += 4;
				bgr24Dest += 3;
			}
		}
	}
	else
	{
		for (int y = 0; y < GetHeight(); y++)
		{
			rowData =(unsigned char *)(nBuffer + y * m_bytesPerRow);
			bgr24Dest = (unsigned char *)(pDataDest + y * nLineSize);
			for (int x = 0; x < GetWidth(); x++)
			{
				bgr24Dest[0] = rowData[3];
				bgr24Dest[1] = rowData[2];
				bgr24Dest[2] = rowData[1];
				rowData += 4;
				bgr24Dest += 3;
			}
		}
	}
	return true;
}

bool wxMacCoreGraphicsBitmapData::SetBGRA( const unsigned char* pBGRA, int nLineSize )
{
	uint8_t* nBuffer = (uint8_t*)BeginRawAccess();
	if(nBuffer == NULL)
	{
		return false;
	}
	uint8_t* rowData ;
	unsigned char* bgraSrc;
	size_t m_bytesPerRow = GetBytesPerRow();
	for (int y = 0; y < GetHeight(); y++)
	{
		rowData =(unsigned char *)(nBuffer + y * m_bytesPerRow);
		bgraSrc = (unsigned char*)(pBGRA) + (y * nLineSize);
		for (int x = 0; x < GetWidth(); x++)
		{
			rowData[0] = bgraSrc[3];
			rowData[1] = bgraSrc[2];
			rowData[2] = bgraSrc[1];
			rowData[3] = bgraSrc[0];
			rowData += 4;
			bgraSrc += 4;
		}
	}
	EndRawAccess();
	return true;
}

bool wxMacCoreGraphicsBitmapData::SetARGB( const unsigned char* pBGRA, int nLineSize )
{
	uint8_t* nBuffer = (uint8_t*)BeginRawAccess();
	if(nBuffer == NULL)
	{
		return false;
	}
	uint8_t* rowData ;
	unsigned char* bgraSrc;
	size_t m_bytesPerRow = GetBytesPerRow();
	size_t m_width = GetWidth();
	for (int y = 0; y < GetHeight(); y++)
	{
		rowData =(unsigned char *)(nBuffer + y * m_bytesPerRow);
		bgraSrc = (unsigned char*)(pBGRA) + (y * nLineSize);
		memcpy(rowData, bgraSrc, m_width * 4);
	}
	EndRawAccess();
	return true;
}

void wxMacCoreGraphicsBitmapData::MakeTransparent()
{
	uint8_t* nBuffer = (uint8_t*)BeginRawAccess();
	if(nBuffer == NULL)
	{
		return;
	}
	uint8_t* rowData ;
	size_t m_bytesPerRow = GetBytesPerRow();
	for (int y = 0; y < GetHeight(); y++)
	{
		rowData =(unsigned char *)(nBuffer + y * m_bytesPerRow);
		memcpy(rowData, 0, m_bytesPerRow);
	}
	SetBGRA(nBuffer, m_bytesPerRow);
	EndRawAccess();
}

void wxMacCoreGraphicsBitmapData::ChangeData(void (*fptr)(void* rowData, int w, void* optionData), void* optionData)
{
	uint8_t* nBuffer = (uint8_t*)BeginRawAccess();
	if(nBuffer == NULL)
	{
		return;
	}
	uint8_t* rowData ;
	size_t m_bytesPerRow = GetBytesPerRow();
	size_t m_width = GetWidth();
	for (int y = 0; y < GetHeight(); y++)
	{
		rowData =(unsigned char *)(nBuffer + y * m_bytesPerRow);
		fptr(rowData, m_width, optionData);
	}
	
	EndRawAccess();
}

#define PIXEL_SIZE 4
#define SIGMA 1

void applyShadowColor(unsigned char * bmpData, int nSrcW, int nSrcH, int Stride, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	unsigned int nR = r;
	unsigned int nG = g;
	unsigned int nB = b;
	unsigned int nA = a;
	for (int i = 0; i < nSrcH; ++i)
	{
		unsigned char * rowData = (unsigned char *)bmpData + (i * Stride);
		for (int j = 0; j < nSrcW; ++j)
		{
			int nColIndex = j * PIXEL_SIZE;
			//Apply alpha for this point
			rowData[nColIndex + 0]	= nA * rowData[nColIndex + 0]/255;
			//Make pre-multiplied values
			rowData[nColIndex + 3]	= nB * rowData[nColIndex + 0]/255;
			rowData[nColIndex + 2]	= nG * rowData[nColIndex + 0]/255;
			rowData[nColIndex + 1]	= nR * rowData[nColIndex + 0]/255;
		}
	}
}

void applyAlphaGaussianBlurHorizontally(const unsigned char * bmpDataSrc, int nSrcW, int nSrcH, int srcStride, int nHozBlurR, unsigned char * bmpDataDes, int nDesW, int nDesH, int desStride)
{
	
	// Allocate space for convolution matrix
	int matrixW = 2 * nHozBlurR + 1;
	double *matrixHoz = new double[matrixW];
	
	// Fill in the convolution matrix
	double matrixSumHoz = 0;
	double dSigma = nHozBlurR/3.0;
	for (int j = 0; j < matrixW; ++j)
	{
		double x = j - nHozBlurR;
		matrixHoz[j] = (exp(-((x * x) / (2.0 * dSigma * dSigma)))) / (2.0 * M_PI * dSigma * dSigma);
		matrixSumHoz += matrixHoz[j];
	}
	
	const int nAlphaIndex = 0;
	//Blur horizontally
	for (int i = 0; i < nDesH; ++i)
	{
		unsigned char * rowDataDes =(unsigned char *)bmpDataDes+ (i * desStride);
		for (int j = 0; j < nDesW; ++j)
		{
			int nColIndex = j * PIXEL_SIZE;
			// Compute filtered value
			double dFilteredValue = 0;
			for (int n = 0; n < matrixW; ++n)
			{
				int x = (j - nHozBlurR + n) - nHozBlurR; // - nHozBlurR ->Map to source image
				if (x < 0 || x >= nSrcW)
					continue;
				dFilteredValue += ((unsigned char *)bmpDataSrc)[(i * srcStride) + (x * PIXEL_SIZE) + nAlphaIndex] * matrixHoz[n];
			}
			// Store result
			rowDataDes[nColIndex + nAlphaIndex] = (dFilteredValue / matrixSumHoz);
		}
	}
	// Deallocate convolution matrix
	delete[] matrixHoz;
}

void applyAlphaGaussianBlurVertically(const unsigned char * bmpDataSrc, int nSrcW, int nSrcH, int srcStride, int nVerBlurR, unsigned char * bmpDataDes, int nDesW, int nDesH, int desStride)
{
	
	// Allocate space for convolution matrix
	int matrixH = 2 * nVerBlurR + 1;
	double *matrixVer = new double[matrixH];
	
	double matrixSumVer = 0;
	double dSigma = nVerBlurR/3.0;
	for (int i = 0; i < matrixH; ++i)
	{
		double x = i - nVerBlurR;
		matrixVer[i] = (exp(-((x * x) / (2.0 * dSigma * dSigma)))) / (2.0 * M_PI * dSigma * dSigma);
		matrixSumVer += matrixVer[i];
	}
	
	const int nAlphaIndex = 0;
	//Blur vertically
	for (int i = 0; i < nDesH; ++i)
	{
		unsigned char * rowDataDes =(unsigned char *)bmpDataDes + (i * desStride);
		for (int j = 0; j < nDesW; ++j)
		{
			int nColIndex = j * PIXEL_SIZE;
			// Compute filtered value
			double dFilteredValue = 0;
			for (int m = 0; m < matrixH; ++m)
			{
				int y = (i - nVerBlurR + m) - nVerBlurR;// - nVerBlurR ->Map to source image
				if (y < 0 || y >= nSrcH)
					continue;
				dFilteredValue += ((unsigned char *)bmpDataSrc)[(y * srcStride) + (j * PIXEL_SIZE) + nAlphaIndex] * matrixVer[m];
			}
			// Store result
			rowDataDes[nColIndex + nAlphaIndex] = (dFilteredValue / matrixSumVer);
		}
	}
	// Deallocate convolution matrix
	delete[] matrixVer;
}

wxMacCoreGraphicsBitmapData *wxMacCoreGraphicsBitmapData::CreateShadowData(int nBlurRadius, unsigned char r, unsigned char g, unsigned char b, unsigned int a)
{ 
	if (m_hBitmap == NULL)
		return NULL;
	size_t m_width = GetWidth() ;
	size_t m_height = GetHeight() ;
	size_t m_bytesPerRow = GetBytesPerRow() ;
	if (nBlurRadius <= 0)
	{
		wxMacCoreGraphicsBitmapData* pRetData = new wxMacCoreGraphicsBitmapData(GetRenderer(), *this);
		unsigned char * data = (unsigned char *)pRetData->GetRawAccess();
		applyShadowColor(data, m_width, m_height, pRetData->GetBytesPerRow(), r, g, b, a);
		return pRetData;
	}
	else
	{
		wxMacCoreGraphicsBitmapData* pTmpData = new wxMacCoreGraphicsBitmapData(GetRenderer());
		pTmpData->Create(m_width + 2 * nBlurRadius, m_height, 32, m_scaleFactor);
		wxMacCoreGraphicsBitmapData* pRetData = new wxMacCoreGraphicsBitmapData(GetRenderer());
		pRetData->Create(m_width + 2 * nBlurRadius, m_height + 2 * nBlurRadius, 32, m_scaleFactor);
		
		
		unsigned char * bmpDataSrc = (unsigned char *)BeginRawAccess();
		unsigned char * bmpDataTmp = (unsigned char *)pTmpData->BeginRawAccess();
		int m_widthTmp = pTmpData->GetWidth() ;
		int m_heightTmp = pTmpData->GetHeight() ;
		size_t m_bytesPerRowTmp = pTmpData->GetBytesPerRow() ;
		int m_widthRet = pRetData->GetWidth() ;
		int m_heightRet = pRetData->GetHeight() ;
		size_t m_bytesPerRowRet = pRetData->GetBytesPerRow() ;
		applyAlphaGaussianBlurHorizontally(bmpDataSrc, m_width, m_height, m_bytesPerRow, nBlurRadius, bmpDataTmp, m_widthTmp, m_heightTmp, pTmpData->GetBytesPerRow());
		pTmpData->EndRawAccess();
		bmpDataTmp = (unsigned char *)pTmpData->BeginRawAccess();
		unsigned char * bmpDataDes = (unsigned char *)pRetData->BeginRawAccess();
		applyAlphaGaussianBlurVertically(bmpDataTmp, m_widthTmp, m_heightTmp, m_bytesPerRowTmp, nBlurRadius, bmpDataDes, m_widthRet, m_heightRet, m_bytesPerRowRet);
		
		applyShadowColor(bmpDataDes, m_widthRet, m_heightRet, m_bytesPerRowRet, r, g, b, a);
		
		pRetData->EndRawAccess();
		pTmpData->EndRawAccess();
		EndRawAccess();
		delete pTmpData;
		return pRetData;
	}
}

void applyGaussianBlurVertically(const unsigned char * bmpDataSrc, int nSrcW, int nSrcH, int nSrcStride, int nVerBlurR, unsigned char * bmpDataDes, int nDesW, int nDesH, int nDesStride)
{
	
	// Allocate space for convolution matrix
	int matrixH = 2 * nVerBlurR + 1;
	double *matrixVer = new double[matrixH];
	
	double matrixSumVer = 0;
	double dSigma = nVerBlurR/3.0;
	for (int i = 0; i < matrixH; ++i)
	{
		double x = i - nVerBlurR;
		matrixVer[i] = (exp(-((x * x) / (2.0 * dSigma * dSigma)))) / (2.0 * M_PI * dSigma * dSigma);
		matrixSumVer += matrixVer[i];
	}
	
	//Blur vertically
	for (int i = 0; i < nDesH; ++i)
	{
		unsigned char * rowDataDes =(unsigned char *)bmpDataDes + (i * nDesStride);
		for (int j = 0; j < nDesW; ++j)
		{
			int nColIndex = j * PIXEL_SIZE;
			// Compute filtered value
			double dFilteredValue0 = 0;
			double dFilteredValue1 = 0;
			double dFilteredValue2 = 0;
			double dFilteredValue3 = 0;
			for (int m = 0; m < matrixH; ++m)
			{
				int y = (i - nVerBlurR + m) - nVerBlurR;// - nVerBlurR ->Map to source image
				if (y < 0 || y >= nSrcH)
					continue;
				int nIndex = (y * nSrcStride) + (j * PIXEL_SIZE);
				dFilteredValue0 += ((unsigned char *)bmpDataSrc)[nIndex]		* matrixVer[m];
				dFilteredValue1 += ((unsigned char *)bmpDataSrc)[nIndex + 1]	* matrixVer[m];
				dFilteredValue2 += ((unsigned char *)bmpDataSrc)[nIndex + 2]	* matrixVer[m];
				dFilteredValue3 += ((unsigned char *)bmpDataSrc)[nIndex + 3]	* matrixVer[m];
			}
			// Store result
			rowDataDes[nColIndex]		= (dFilteredValue0 / matrixSumVer);
			rowDataDes[nColIndex + 1]	= (dFilteredValue1 / matrixSumVer);
			rowDataDes[nColIndex + 2]	= (dFilteredValue2 / matrixSumVer);
			rowDataDes[nColIndex + 3]	= (dFilteredValue3 / matrixSumVer);
		}
	}
	// Deallocate convolution matrix
	delete[] matrixVer;
}

void applyGaussianBlurHorizontally(const unsigned char * bmpDataSrc, int nSrcW, int nSrcH, int nSrcStride, int nHozBlurR, unsigned char * bmpDataDes, int nDesW, int nDesH, int nDesStride)
{
	
	// Allocate space for convolution matrix
	int matrixW = 2 * nHozBlurR + 1;
	double *matrixHoz = new double[matrixW];
	
	// Fill in the convolution matrix
	double matrixSumHoz = 0;
	double dSigma = nHozBlurR/3.0;
	for (int j = 0; j < matrixW; ++j)
	{
		double x = j - nHozBlurR;
		matrixHoz[j] = (exp(-((x * x) / (2.0 * dSigma * dSigma)))) / (2.0 * M_PI * dSigma * dSigma);
		matrixSumHoz += matrixHoz[j];
	}
	
	//Blur horizontally
	for (int i = 0; i < nDesH; ++i)
	{
		unsigned char * rowDataDes =(unsigned char *)bmpDataDes + (i * nDesStride);
		for (int j = 0; j < nDesW; ++j)
		{
			int nColIndex = j * PIXEL_SIZE;
			// Compute filtered value
			double dFilteredValue0 = 0;
			double dFilteredValue1 = 0;
			double dFilteredValue2 = 0;
			double dFilteredValue3 = 0;
			for (int n = 0; n < matrixW; ++n)
			{
				int x = (j - nHozBlurR + n) - nHozBlurR; // - nHozBlurR ->Map to source image
				if (x < 0 || x >= nSrcW)
					continue;
				int nIndex = (i * nSrcStride) + (x * PIXEL_SIZE);
				dFilteredValue0 += ((unsigned char *)bmpDataSrc)[nIndex] * matrixHoz[n];
				dFilteredValue1 += ((unsigned char *)bmpDataSrc)[nIndex + 1] * matrixHoz[n];
				dFilteredValue2 += ((unsigned char *)bmpDataSrc)[nIndex + 2] * matrixHoz[n];
				dFilteredValue3 += ((unsigned char *)bmpDataSrc)[nIndex + 3] * matrixHoz[n];
			}
			// Store result
			rowDataDes[nColIndex]		= (dFilteredValue0 / matrixSumHoz);
			rowDataDes[nColIndex + 1]	= (dFilteredValue1 / matrixSumHoz);
			rowDataDes[nColIndex + 2]	= (dFilteredValue2 / matrixSumHoz);
			rowDataDes[nColIndex + 3]	= (dFilteredValue3 / matrixSumHoz);
		}
	}
	// Deallocate convolution matrix
	delete[] matrixHoz;
}

wxMacCoreGraphicsBitmapData *wxMacCoreGraphicsBitmapData::CreateBlurredData(int nBlurRadiusH, int nBlurRadiusV)
{ 
	if (GetRawAccess() == NULL)
		return NULL;
	if (nBlurRadiusH <= 0 && nBlurRadiusV <= 0)
	{
		return new wxMacCoreGraphicsBitmapData(GetRenderer(), *this);;
	}
	else
	{
		wxMacCoreGraphicsBitmapData* pRetData = NULL;
		wxMacCoreGraphicsBitmapData* pSrcData = this;
		
		bool bTwoPass = (nBlurRadiusH >0 && nBlurRadiusV > 0);
		
		if (nBlurRadiusH > 0)
		{
			pRetData = new wxMacCoreGraphicsBitmapData(GetRenderer(), pSrcData->GetWidth() + 2 * nBlurRadiusH, pSrcData->GetHeight(), m_scaleFactor);
			
			//Blur horizontally
			
			unsigned char * bmpDataSrc = (unsigned char *)pSrcData->GetRawAccess();
			unsigned char * bmpDataDes = (unsigned char *)pRetData->BeginRawAccess();
			
			applyGaussianBlurHorizontally(bmpDataSrc, pSrcData->GetWidth(), pSrcData->GetHeight(), pSrcData->GetBytesPerRow(), nBlurRadiusH, bmpDataDes, pRetData->GetWidth(), pRetData->GetHeight(), pRetData->GetBytesPerRow());
			pRetData->EndRawAccess();
		}
		if (bTwoPass)
			pSrcData = pRetData;
		
		if (nBlurRadiusV > 0)
		{
			pRetData = new wxMacCoreGraphicsBitmapData(GetRenderer(), pSrcData->GetWidth(), pSrcData->GetHeight() + 2 * nBlurRadiusV, m_scaleFactor);
			
			//Blur horizontally
			
			unsigned char * bmpDataSrc = (unsigned char *)pSrcData->GetRawAccess();
			unsigned char * bmpDataDes = (unsigned char *)pRetData->BeginRawAccess();
			
			applyGaussianBlurVertically(bmpDataSrc, pSrcData->GetWidth(), pSrcData->GetHeight(), pSrcData->GetBytesPerRow(), nBlurRadiusV, bmpDataDes, pRetData->GetWidth(), pRetData->GetHeight(), pRetData->GetBytesPerRow());
			pRetData->EndRawAccess();
		}
		if (bTwoPass)
			delete pSrcData;
		return pRetData;
	}
	return NULL;
}

WXImage wxMacCoreGraphicsBitmapData::GetImage() const
{ 
	wxCHECK_MSG( IsOk() , 0 , "Invalid Bitmap");
	
	if ( !m_nsImage )
	{
		wxCFRef< CGImageRef > cgimage(CreateCGImage());
		return wxOSXGetImageFromCGImage( cgimage, GetScaleFactor(), IsTemplate() );
	}
	
	return m_nsImage;
}


//
// Graphics Matrix
//

//-----------------------------------------------------------------------------
// wxMacCoreGraphicsMatrix declaration
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxMacCoreGraphicsMatrixData : public wxGraphicsMatrixData
{
public :
    wxMacCoreGraphicsMatrixData(wxGraphicsRenderer* renderer) ;

    virtual ~wxMacCoreGraphicsMatrixData() ;

    virtual wxGraphicsObjectRefData *Clone() const wxOVERRIDE ;

    // concatenates the matrix
    virtual void Concat( const wxGraphicsMatrixData *t ) wxOVERRIDE;

    // sets the matrix to the respective values
    virtual void Set(wxDouble a=1.0, wxDouble b=0.0, wxDouble c=0.0, wxDouble d=1.0,
        wxDouble tx=0.0, wxDouble ty=0.0) wxOVERRIDE;

    // gets the component valuess of the matrix
    virtual void Get(wxDouble* a=NULL, wxDouble* b=NULL,  wxDouble* c=NULL,
                     wxDouble* d=NULL, wxDouble* tx=NULL, wxDouble* ty=NULL) const wxOVERRIDE;

    // makes this the inverse matrix
    virtual void Invert() wxOVERRIDE;

    // returns true if the elements of the transformation matrix are equal ?
    virtual bool IsEqual( const wxGraphicsMatrixData* t) const wxOVERRIDE ;

    // return true if this is the identity matrix
    virtual bool IsIdentity() const wxOVERRIDE;

    //
    // transformation
    //

    // add the translation to this matrix
    virtual void Translate( wxDouble dx , wxDouble dy ) wxOVERRIDE;

    // add the scale to this matrix
    virtual void Scale( wxDouble xScale , wxDouble yScale ) wxOVERRIDE;

    // add the rotation to this matrix (radians)
    virtual void Rotate( wxDouble angle ) wxOVERRIDE;

    //
    // apply the transforms
    //

    // applies that matrix to the point
    virtual void TransformPoint( wxDouble *x, wxDouble *y ) const wxOVERRIDE;

    // applies the matrix except for translations
    virtual void TransformDistance( wxDouble *dx, wxDouble *dy ) const wxOVERRIDE;

    // returns the native representation
    virtual void * GetNativeMatrix() const wxOVERRIDE;

private :
    CGAffineTransform m_matrix;
} ;

//-----------------------------------------------------------------------------
// wxMacCoreGraphicsMatrix implementation
//-----------------------------------------------------------------------------

wxMacCoreGraphicsMatrixData::wxMacCoreGraphicsMatrixData(wxGraphicsRenderer* renderer) : wxGraphicsMatrixData(renderer)
{
}

wxMacCoreGraphicsMatrixData::~wxMacCoreGraphicsMatrixData()
{
}

wxGraphicsObjectRefData *wxMacCoreGraphicsMatrixData::Clone() const
{
    wxMacCoreGraphicsMatrixData* m = new wxMacCoreGraphicsMatrixData(GetRenderer()) ;
    m->m_matrix = m_matrix ;
    return m;
}

// concatenates the matrix
void wxMacCoreGraphicsMatrixData::Concat( const wxGraphicsMatrixData *t )
{
    m_matrix = CGAffineTransformConcat(*((CGAffineTransform*) t->GetNativeMatrix()), m_matrix );
}

// sets the matrix to the respective values
void wxMacCoreGraphicsMatrixData::Set(wxDouble a, wxDouble b, wxDouble c, wxDouble d,
    wxDouble tx, wxDouble ty)
{
    m_matrix = CGAffineTransformMake((CGFloat) a,(CGFloat) b,(CGFloat) c,(CGFloat) d,(CGFloat) tx,(CGFloat) ty);
}

// gets the component valuess of the matrix
void wxMacCoreGraphicsMatrixData::Get(wxDouble* a, wxDouble* b,  wxDouble* c,
                                      wxDouble* d, wxDouble* tx, wxDouble* ty) const
{
    if (a)  *a = m_matrix.a;
    if (b)  *b = m_matrix.b;
    if (c)  *c = m_matrix.c;
    if (d)  *d = m_matrix.d;
    if (tx) *tx= m_matrix.tx;
    if (ty) *ty= m_matrix.ty;
}

// makes this the inverse matrix
void wxMacCoreGraphicsMatrixData::Invert()
{
    m_matrix = CGAffineTransformInvert( m_matrix );
}

// returns true if the elements of the transformation matrix are equal ?
bool wxMacCoreGraphicsMatrixData::IsEqual( const wxGraphicsMatrixData* t) const
{
    return CGAffineTransformEqualToTransform(m_matrix, *((CGAffineTransform*) t->GetNativeMatrix()));
}

// return true if this is the identity matrix
bool wxMacCoreGraphicsMatrixData::IsIdentity() const
{
    return ( m_matrix.a == 1 && m_matrix.d == 1 &&
        m_matrix.b == 0 && m_matrix.d == 0 && m_matrix.tx == 0 && m_matrix.ty == 0);
}

//
// transformation
//

// add the translation to this matrix
void wxMacCoreGraphicsMatrixData::Translate( wxDouble dx , wxDouble dy )
{
    m_matrix = CGAffineTransformTranslate( m_matrix, (CGFloat) dx, (CGFloat) dy);
}

// add the scale to this matrix
void wxMacCoreGraphicsMatrixData::Scale( wxDouble xScale , wxDouble yScale )
{
    m_matrix = CGAffineTransformScale( m_matrix, (CGFloat) xScale, (CGFloat) yScale);
}

// add the rotation to this matrix (radians)
void wxMacCoreGraphicsMatrixData::Rotate( wxDouble angle )
{
	CGFloat angleRadial = (angle/360)*2*pi;
    m_matrix = CGAffineTransformRotate( m_matrix, (CGFloat) angleRadial);
}

//
// apply the transforms
//

// applies that matrix to the point
void wxMacCoreGraphicsMatrixData::TransformPoint( wxDouble *x, wxDouble *y ) const
{
    CGPoint pt = CGPointApplyAffineTransform( CGPointMake((CGFloat) *x,(CGFloat) *y), m_matrix);

    *x = pt.x;
    *y = pt.y;
}

// applies the matrix except for translations
void wxMacCoreGraphicsMatrixData::TransformDistance( wxDouble *dx, wxDouble *dy ) const
{
    CGSize sz = CGSizeApplyAffineTransform( CGSizeMake((CGFloat) *dx,(CGFloat) *dy) , m_matrix );
    *dx = sz.width;
    *dy = sz.height;
}

// returns the native representation
void * wxMacCoreGraphicsMatrixData::GetNativeMatrix() const
{
    return (void*) &m_matrix;
}

//
// Graphics Path
//

//-----------------------------------------------------------------------------
// wxMacCoreGraphicsPath declaration
//-----------------------------------------------------------------------------

class WXDLLEXPORT wxMacCoreGraphicsPathData : public wxGraphicsPathData
{
public :
    wxMacCoreGraphicsPathData( wxGraphicsRenderer* renderer, CGMutablePathRef path = NULL);

    ~wxMacCoreGraphicsPathData();

    virtual wxGraphicsObjectRefData *Clone() const wxOVERRIDE;

    // begins a new subpath at (x,y)
    virtual void MoveToPoint( wxDouble x, wxDouble y ) wxOVERRIDE;

    // adds a straight line from the current point to (x,y)
    virtual void AddLineToPoint( wxDouble x, wxDouble y ) wxOVERRIDE;

    // adds a cubic Bezier curve from the current point, using two control points and an end point
    virtual void AddCurveToPoint( wxDouble cx1, wxDouble cy1, wxDouble cx2, wxDouble cy2, wxDouble x, wxDouble y ) wxOVERRIDE;

    // closes the current sub-path
    virtual void CloseSubpath() wxOVERRIDE;

    // gets the last point of the current path, (0,0) if not yet set
    virtual void GetCurrentPoint( wxDouble* x, wxDouble* y) const wxOVERRIDE;

    // adds an arc of a circle centering at (x,y) with radius (r) from startAngle to endAngle
    virtual void AddArc( wxDouble x, wxDouble y, wxDouble r, wxDouble startAngle, wxDouble endAngle, bool clockwise ) wxOVERRIDE;

    //
    // These are convenience functions which - if not available natively will be assembled
    // using the primitives from above
    //

    // adds a quadratic Bezier curve from the current point, using a control point and an end point
    virtual void AddQuadCurveToPoint( wxDouble cx, wxDouble cy, wxDouble x, wxDouble y ) wxOVERRIDE;

    // appends a rectangle as a new closed subpath
    virtual void AddRectangle( wxDouble x, wxDouble y, wxDouble w, wxDouble h ) wxOVERRIDE;

    // appends a circle as a new closed subpath
    virtual void AddCircle( wxDouble x, wxDouble y, wxDouble r ) wxOVERRIDE;

    // appends an ellipsis as a new closed subpath fitting the passed rectangle
    virtual void AddEllipse( wxDouble x, wxDouble y, wxDouble w, wxDouble h) wxOVERRIDE;

    // draws a an arc to two tangents connecting (current) to (x1,y1) and (x1,y1) to (x2,y2), also a straight line from (current) to (x1,y1)
    virtual void AddArcToPoint( wxDouble x1, wxDouble y1 , wxDouble x2, wxDouble y2, wxDouble r ) wxOVERRIDE;

    // adds another path
    virtual void AddPath( const wxGraphicsPathData* path ) wxOVERRIDE;

    // returns the native path
    virtual void * GetNativePath() const wxOVERRIDE { return m_path; }

    // give the native path returned by GetNativePath() back (there might be some deallocations necessary)
    virtual void UnGetNativePath(void *WXUNUSED(p)) const wxOVERRIDE {}

    // transforms each point of this path by the matrix
    virtual void Transform( const wxGraphicsMatrixData* matrix ) wxOVERRIDE;

    // gets the bounding box enclosing all points (possibly including control points)
    virtual void GetBox(wxDouble *x, wxDouble *y, wxDouble *w, wxDouble *h) const wxOVERRIDE;

    //gets the bounding box including the width of the pen
    virtual void GetWidenedBox(const wxGraphicsPenData* pen, const wxGraphicsMatrixData* matrix, wxDouble *x, wxDouble *y, wxDouble *w, wxDouble *h) const wxOVERRIDE;
    
    virtual bool Contains( wxDouble x, wxDouble y, wxPolygonFillMode fillStyle = wxODDEVEN_RULE) const wxOVERRIDE;
    
    virtual bool OutlineContains(wxDouble x, wxDouble y, const wxGraphicsPenData* pen) const wxOVERRIDE;
    
    virtual void ConvertToStrokePath(const wxGraphicsPenData* pen) wxOVERRIDE;
private :
    CGMutablePathRef m_path;
};

//-----------------------------------------------------------------------------
// wxMacCoreGraphicsPath implementation
//-----------------------------------------------------------------------------

wxMacCoreGraphicsPathData::wxMacCoreGraphicsPathData( wxGraphicsRenderer* renderer, CGMutablePathRef path) : wxGraphicsPathData(renderer)
{
    if ( path )
        m_path = path;
    else
        m_path = CGPathCreateMutable();
}

wxMacCoreGraphicsPathData::~wxMacCoreGraphicsPathData()
{
    CGPathRelease( m_path );
}

wxGraphicsObjectRefData* wxMacCoreGraphicsPathData::Clone() const
{
    wxMacCoreGraphicsPathData* clone = new wxMacCoreGraphicsPathData(GetRenderer(),CGPathCreateMutableCopy(m_path));
    return clone ;
}


// opens (starts) a new subpath
void wxMacCoreGraphicsPathData::MoveToPoint( wxDouble x1 , wxDouble y1 )
{
    CGPathMoveToPoint( m_path , NULL , (CGFloat) x1 , (CGFloat) y1 );
}

void wxMacCoreGraphicsPathData::AddLineToPoint( wxDouble x1 , wxDouble y1 )
{
    // This function should behave as MoveToPoint if current point is not yet set
    // (CGPathAddLineToPoint requires non-empty path).
    if ( CGPathIsEmpty(m_path) )
    {
        MoveToPoint(x1, y1);
    }
    else
    {
        CGPathAddLineToPoint( m_path , NULL , (CGFloat) x1 , (CGFloat) y1 );
    }
}

void wxMacCoreGraphicsPathData::AddCurveToPoint( wxDouble cx1, wxDouble cy1, wxDouble cx2, wxDouble cy2, wxDouble x, wxDouble y )
{
    // This function should be preceded by MoveToPoint(cx1, cy1)
    // if current point is not yet set (CGPathAddCurveToPoint requires non-empty path).
    if ( CGPathIsEmpty(m_path) )
    {
        MoveToPoint(cx1, cy1);
    }
    CGPathAddCurveToPoint( m_path , NULL , (CGFloat) cx1 , (CGFloat) cy1 , (CGFloat) cx2, (CGFloat) cy2, (CGFloat) x , (CGFloat) y );
}

void wxMacCoreGraphicsPathData::AddQuadCurveToPoint( wxDouble cx1, wxDouble cy1, wxDouble x, wxDouble y )
{
    // This function should be preceded by MoveToPoint(cx1, cy1)
    // if current point is not yet set (CGPathAddQuadCurveToPoint requires non-empty path).
    if ( CGPathIsEmpty(m_path) )
    {
        MoveToPoint(cx1, cy1);
    }
    CGPathAddQuadCurveToPoint( m_path , NULL , (CGFloat) cx1 , (CGFloat) cy1 , (CGFloat) x , (CGFloat) y );
}

void wxMacCoreGraphicsPathData::AddRectangle( wxDouble x, wxDouble y, wxDouble w, wxDouble h )
{
    CGRect cgRect = { { (CGFloat) x , (CGFloat) y } , { (CGFloat) w , (CGFloat) h } };
    CGPathAddRect( m_path , NULL , cgRect );
}

void wxMacCoreGraphicsPathData::AddCircle( wxDouble x, wxDouble y , wxDouble r )
{
    CGPathAddEllipseInRect( m_path, NULL, CGRectMake(x-r,y-r,2*r,2*r));
}

void wxMacCoreGraphicsPathData::AddEllipse( wxDouble x, wxDouble y, wxDouble w, wxDouble h )
{
    CGPathAddEllipseInRect( m_path, NULL, CGRectMake(x,y,w,h));
}

// adds an arc of a circle centering at (x,y) with radius (r) from startAngle to endAngle
void wxMacCoreGraphicsPathData::AddArc( wxDouble x, wxDouble y, wxDouble r, wxDouble startAngle, wxDouble endAngle, bool clockwise )
{
    // inverse direction as we the 'normal' state is a y axis pointing down, ie mirrored to the standard core graphics setup
    CGPathAddArc( m_path, NULL , (CGFloat) x, (CGFloat) y, (CGFloat) r, (CGFloat) startAngle, (CGFloat) endAngle, !clockwise);
}

void wxMacCoreGraphicsPathData::AddArcToPoint( wxDouble x1, wxDouble y1 , wxDouble x2, wxDouble y2, wxDouble r )
{
    // This function should be preceded by MoveToPoint(0, 0)
    // if current point is not yet set (CGPathAddArcToPoint requires non-empty path).
    if ( CGPathIsEmpty(m_path) )
    {
        MoveToPoint(0, 0);
    }
    CGPathAddArcToPoint( m_path, NULL , (CGFloat) x1, (CGFloat) y1, (CGFloat) x2, (CGFloat) y2, (CGFloat) r);
}

void wxMacCoreGraphicsPathData::AddPath( const wxGraphicsPathData* path )
{
    CGPathAddPath( m_path , NULL, (CGPathRef) path->GetNativePath() );
}

// closes the current subpath
void wxMacCoreGraphicsPathData::CloseSubpath()
{
    if ( !CGPathIsEmpty(m_path) )
    {
        CGPathCloseSubpath( m_path );
    }
}

// gets the last point of the current path, (0,0) if not yet set
void wxMacCoreGraphicsPathData::GetCurrentPoint( wxDouble* x, wxDouble* y) const
{
    CGPoint p;
    if ( CGPathIsEmpty(m_path) )
    {
        p.x = p.y = 0;
    }
    else
    {
        p = CGPathGetCurrentPoint(m_path);
    }
    *x = p.x;
    *y = p.y;
}

// transforms each point of this path by the matrix
void wxMacCoreGraphicsPathData::Transform( const wxGraphicsMatrixData* matrix )
{
    CGMutablePathRef p = CGPathCreateMutable() ;
    CGPathAddPath( p, (CGAffineTransform*) matrix->GetNativeMatrix() , m_path );
    CGPathRelease( m_path );
    m_path = p;
}

// gets the bounding box enclosing all points (possibly including control points)
void wxMacCoreGraphicsPathData::GetBox(wxDouble *x, wxDouble *y, wxDouble *w, wxDouble *h) const
{
    CGRect bounds = CGPathGetBoundingBox( m_path ) ;
    if ( CGRectIsEmpty(bounds) )
    {
        bounds = CGRectZero;
    }

    *x = bounds.origin.x;
    *y = bounds.origin.y;
    *w = bounds.size.width;
    *h = bounds.size.height;
}

void wxMacCoreGraphicsPathData::GetWidenedBox(const wxGraphicsPenData* pen, const wxGraphicsMatrixData* matrix, wxDouble *x, wxDouble *y, wxDouble *w, wxDouble *h) const
{
	CGMutablePathRef* matrixNative = NULL;
	
	if (matrix)
		matrixNative = (CGMutablePathRef*)matrix->GetNativeMatrix();
	
	CGRect bounds;
	bounds = CGPathGetPathBoundingBox(m_path);
	*x = bounds.origin.x;
	*y = bounds.origin.y;
	*w = bounds.size.width;
	*h = bounds.size.height;
}

bool wxMacCoreGraphicsPathData::Contains( wxDouble x, wxDouble y, wxPolygonFillMode fillStyle) const
{
    return CGPathContainsPoint( m_path, NULL, CGPointMake((CGFloat) x,(CGFloat) y), fillStyle == wxODDEVEN_RULE );
}

bool wxMacCoreGraphicsPathData::OutlineContains( wxDouble x, wxDouble y, const wxGraphicsPenData* pen ) const
{
    CGPathRef strokedPath = CGPathCreateCopyByStrokingPath(m_path, NULL, 15,
                                                           kCGLineCapRound, kCGLineJoinRound, 1);
    bool bIsContains = CGPathContainsPoint(strokedPath, NULL, CGPointMake(x, y), false);
    CGPathRelease(strokedPath);
    return bIsContains;
}

void wxMacCoreGraphicsPathData::ConvertToStrokePath( const wxGraphicsPenData* pen /*= NULL*/ )
{
    const wxMacCoreGraphicsPenData* penData = (wxMacCoreGraphicsPenData*)pen;
    if (m_path)
    {
        CGPathRef cgPathCopy = CGPathCreateCopyByDashingPath(m_path, NULL, 0, penData->GetLengths(), penData->GetCount());
        m_path = (CGMutablePathRef) CGPathCreateCopyByStrokingPath(cgPathCopy, NULL, penData->GetWidth(), penData->GetCap(), penData->GetJoin(), 10);
        CGPathRelease(cgPathCopy);
    }
}

//
// Graphics Context
//

//-----------------------------------------------------------------------------
// wxMacCoreGraphicsContext declaration
//-----------------------------------------------------------------------------

class WXDLLEXPORT wxMacCoreGraphicsContext : public wxGraphicsContext
{
public:
    wxMacCoreGraphicsContext( wxGraphicsRenderer* renderer,
                              CGContextRef cgcontext,
                              wxDouble width = 0,
                              wxDouble height = 0,
                              wxWindow* window = NULL );

    wxMacCoreGraphicsContext( wxGraphicsRenderer* renderer, wxWindow* window );

    wxMacCoreGraphicsContext( wxGraphicsRenderer* renderer);
	
	wxMacCoreGraphicsContext( wxGraphicsRenderer* renderer, wxGraphicsBitmap& bitmap);

    ~wxMacCoreGraphicsContext();

    // Enable offset on non-high DPI displays, i.e. those with scale factor <= 1.
    void SetEnableOffsetFromScaleFactor(double factor)
    {
        m_enableOffset = factor <= 1.0;
    }

    void Init();

    virtual void StartPage( wxDouble width, wxDouble height ) wxOVERRIDE;

    virtual void EndPage() wxOVERRIDE;

    virtual void Flush() wxOVERRIDE;

    // push the current state of the context, ie the transformation matrix on a stack
    virtual void PushState() wxOVERRIDE;

    // pops a stored state from the stack
    virtual void PopState() wxOVERRIDE;

    // clips drawings to the region
    virtual void Clip( const wxRegion &region ) wxOVERRIDE;

    // clips drawings to the rect
    virtual void Clip( wxDouble x, wxDouble y, wxDouble w, wxDouble h ) wxOVERRIDE;

    // resets the clipping to original extent
    virtual void ResetClip() wxOVERRIDE;

    // returns bounding box of the clipping region
    virtual void GetClipBox(wxDouble* x, wxDouble* y, wxDouble* w, wxDouble* h) wxOVERRIDE;

    virtual void * GetNativeContext() wxOVERRIDE;

    virtual bool SetAntialiasMode(wxAntialiasMode antialias) wxOVERRIDE;

    virtual bool SetInterpolationQuality(wxInterpolationQuality interpolation) wxOVERRIDE;

    virtual bool SetCompositionMode(wxCompositionMode op) wxOVERRIDE;

    virtual void BeginLayer(wxDouble opacity) wxOVERRIDE;

    virtual void EndLayer() wxOVERRIDE;

    //
    // transformation
    //

    // translate
    virtual void Translate( wxDouble dx , wxDouble dy ) wxOVERRIDE;

    // scale
    virtual void Scale( wxDouble xScale , wxDouble yScale ) wxOVERRIDE;

    // rotate (radians)
    virtual void Rotate( wxDouble angle ) wxOVERRIDE;

    // concatenates this transform with the current transform of this context
    virtual void ConcatTransform( const wxGraphicsMatrix& matrix ) wxOVERRIDE;

    // sets the transform of this context
    virtual void SetTransform( const wxGraphicsMatrix& matrix ) wxOVERRIDE;

    // gets the matrix of this context
    virtual wxGraphicsMatrix GetTransform() const wxOVERRIDE;
    //
    // setting the paint
    //

    // strokes along a path with the current pen
    virtual void StrokePath( const wxGraphicsPath &path ) wxOVERRIDE;

    // fills a path with the current brush
    virtual void FillPath( const wxGraphicsPath &path, wxPolygonFillMode fillStyle = wxODDEVEN_RULE ) wxOVERRIDE;

    // draws a path by first filling and then stroking
    virtual void DrawPath( const wxGraphicsPath &path, wxPolygonFillMode fillStyle = wxODDEVEN_RULE ) wxOVERRIDE;

    // paints a transparent rectangle (only useful for bitmaps or windows)
    virtual void ClearRectangle(wxDouble x, wxDouble y, wxDouble w, wxDouble h) wxOVERRIDE;

    virtual bool ShouldOffset() const wxOVERRIDE
    {
        if ( !m_enableOffset )
            return false;

        int penwidth = 0 ;
        if ( !m_pen.IsNull() )
        {
            penwidth = (int)((wxMacCoreGraphicsPenData*)m_pen.GetRefData())->GetWidth();
            if ( penwidth == 0 )
                penwidth = 1;
        }
        return ( penwidth % 2 ) == 1;
    }
    //
    // text
    //

    virtual void GetTextExtent( const wxString &text, wxDouble *width, wxDouble *height,
        wxDouble *descent, wxDouble *externalLeading ) const wxOVERRIDE;

    virtual void GetPartialTextExtents(const wxString& text, wxArrayDouble& widths) const wxOVERRIDE;

    //
    // image support
    //

    virtual void DrawBitmap( const wxBitmap &bmp, wxDouble x, wxDouble y, wxDouble w, wxDouble h ) wxOVERRIDE;

    virtual void DrawBitmap( const wxGraphicsBitmap &bmp, wxDouble x, wxDouble y, wxDouble w, wxDouble h ) wxOVERRIDE;

    virtual void DrawIcon( const wxIcon &icon, wxDouble x, wxDouble y, wxDouble w, wxDouble h ) wxOVERRIDE;
	
	virtual void DrawBitmap( const wxGraphicsBitmap &bmp, const wxRect2DDouble& rcSrc, const wxRect2DDouble& rcDest) wxOVERRIDE;

    // fast convenience methods


    virtual void DrawRectangle( wxDouble x, wxDouble y, wxDouble w, wxDouble h ) wxOVERRIDE;

    void SetNativeContext( CGContextRef cg );

    wxDECLARE_NO_COPY_CLASS(wxMacCoreGraphicsContext);

private:
    bool EnsureIsValid();
    void CheckInvariants() const;
    bool DoSetAntialiasMode(wxAntialiasMode antialias);
    bool DoSetInterpolationQuality(wxInterpolationQuality interpolation);
    bool DoSetCompositionMode(wxCompositionMode op);

    virtual void DoDrawText( const wxString &str, wxDouble x, wxDouble y ) wxOVERRIDE;
    virtual void DoDrawRotatedText( const wxString &str, wxDouble x, wxDouble y, wxDouble angle ) wxOVERRIDE;

    CGContextRef m_cgContext;
    WXWidget m_view;
    bool m_contextSynthesized;
    CGAffineTransform m_initTransform;
    CGAffineTransform m_windowTransform;
    bool m_invisible;

#if wxOSX_USE_COCOA_OR_CARBON
    wxCFRef<HIShapeRef> m_clipRgn;
#endif
};

//-----------------------------------------------------------------------------
// device context implementation
//
// more and more of the dc functionality should be implemented by calling
// the appropricate wxMacCoreGraphicsContext, but we will have to do that step by step
// also coordinate conversions should be moved to native matrix ops
//-----------------------------------------------------------------------------

// we always stock two context states, one at entry, to be able to preserve the
// state we were called with, the other one after changing to HI Graphics orientation
// (this one is used for getting back clippings etc)

//-----------------------------------------------------------------------------
// wxMacCoreGraphicsContext implementation
//-----------------------------------------------------------------------------

class wxQuartzOffsetHelper
{
public :
    wxQuartzOffsetHelper( CGContextRef cg , bool offset )
    {
        m_cg = cg;
        m_offset = offset;
        if ( m_offset )
        {
            m_userOffset = CGContextConvertSizeToUserSpace( m_cg, CGSizeMake( 0.5 , 0.5 ) );
            CGContextTranslateCTM( m_cg, m_userOffset.width , m_userOffset.height );
        }
        else
        {
            m_userOffset = CGSizeMake(0.0, 0.0);
        }

    }
    ~wxQuartzOffsetHelper( )
    {
        if ( m_offset )
            CGContextTranslateCTM( m_cg, -m_userOffset.width , -m_userOffset.height );
    }
public :
    CGSize m_userOffset;
    CGContextRef m_cg;
    bool m_offset;
} ;

void wxMacCoreGraphicsContext::Init()
{
    m_cgContext = NULL;
    m_contextSynthesized = false;
    m_width = 0;
    m_height = 0;
#if wxOSX_USE_COCOA_OR_IPHONE
    m_view = NULL;
#endif
    m_invisible = false;
    m_antialias = wxANTIALIAS_DEFAULT;
    m_interpolation = wxINTERPOLATION_BEST;
	m_composition = wxCOMPOSITION_SOURCE;
	wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	wxColour color = wxSystemSettings::GetColour(wxSYS_COLOUR_MENUTEXT);
	SetFont(font.IsOk() ? font : *wxNORMAL_FONT, color.IsOk() ? color : *wxBLACK);
}

wxMacCoreGraphicsContext::wxMacCoreGraphicsContext( wxGraphicsRenderer* renderer,
                                                    CGContextRef cgcontext,
                                                    wxDouble width,
                                                    wxDouble height,
                                                    wxWindow* window )
    : wxGraphicsContext(renderer, window)
{
    Init();
    SetNativeContext(cgcontext);
    m_width = width;
    m_height = height;
    m_initTransform = m_cgContext ? CGContextGetCTM(m_cgContext) : CGAffineTransformIdentity;
}

wxMacCoreGraphicsContext::wxMacCoreGraphicsContext( wxGraphicsRenderer* renderer,
                                                    wxWindow* window )
    : wxGraphicsContext(renderer, window)
{
    Init();

    SetEnableOffsetFromScaleFactor(window->GetContentScaleFactor());
    wxSize sz = window->GetSize();
    m_width = sz.x;
    m_height = sz.y;

#if wxOSX_USE_COCOA_OR_IPHONE
    m_view = window->GetHandle();

#if wxOSX_USE_COCOA
    if ( ! window->GetPeer()->IsFlipped() )
    {
        m_windowTransform = CGAffineTransformMakeTranslation( 0 , m_height );
        m_windowTransform = CGAffineTransformScale( m_windowTransform , 1 , -1 );
    }
    else
#endif
    {
        m_windowTransform = CGAffineTransformIdentity;
    }
#else
    int originX , originY;
    originX = originY = 0;
    Rect bounds = { 0,0,0,0 };
    m_windowRef = (WindowRef) window->MacGetTopLevelWindowRef();
    window->MacWindowToRootWindow( &originX , &originY );
    GetWindowBounds( m_windowRef, kWindowContentRgn, &bounds );
    m_windowTransform = CGAffineTransformMakeTranslation( 0 , bounds.bottom - bounds.top );
    m_windowTransform = CGAffineTransformScale( m_windowTransform , 1 , -1 );
    m_windowTransform = CGAffineTransformTranslate( m_windowTransform, originX, originY ) ;
#endif
    m_initTransform = m_windowTransform;
}

wxMacCoreGraphicsContext::wxMacCoreGraphicsContext(wxGraphicsRenderer* renderer) : wxGraphicsContext(renderer)
{
    Init();
    m_initTransform = CGAffineTransformIdentity;
}

wxMacCoreGraphicsContext::~wxMacCoreGraphicsContext()
{
    SetNativeContext(NULL);
}


void wxMacCoreGraphicsContext::CheckInvariants() const
{
    // check invariants here for debugging ...
}



void wxMacCoreGraphicsContext::StartPage( wxDouble width, wxDouble height )
{
    CGRect r;
    if ( width != 0 && height != 0)
        r = CGRectMake( (CGFloat) 0.0 , (CGFloat) 0.0 , (CGFloat) width  , (CGFloat) height );
    else
        r = CGRectMake( (CGFloat) 0.0 , (CGFloat) 0.0 , (CGFloat) m_width  , (CGFloat) m_height );

    CGContextBeginPage(m_cgContext,  &r );
//    CGContextTranslateCTM( m_cgContext , 0 ,  height == 0 ? m_height : height );
//    CGContextScaleCTM( m_cgContext , 1 , -1 );
}

void wxMacCoreGraphicsContext::EndPage()
{
    CGContextEndPage(m_cgContext);
}

void wxMacCoreGraphicsContext::Flush()
{
    CGContextFlush(m_cgContext);
}

bool wxMacCoreGraphicsContext::EnsureIsValid()
{
    CheckInvariants();

    if ( !m_cgContext )
    {
        if (m_invisible)
            return false;

#if wxOSX_USE_COCOA
        if ( wxOSXLockFocus(m_view) )
        {
            m_cgContext = wxOSXGetContextFromCurrentContext();
            wxASSERT_MSG( m_cgContext != NULL, wxT("Unable to retrieve drawing context from View"));
        }
        else
        {
            m_invisible = true;
        }
#endif
#if wxOSX_USE_IPHONE
        m_cgContext = wxOSXGetContextFromCurrentContext();
        if ( m_cgContext == NULL )
        {
            m_invisible = true;
        }
#endif
        if ( m_cgContext )
        {
            CGContextSaveGState( m_cgContext );
#if wxOSX_USE_COCOA_OR_CARBON
            if ( m_clipRgn.get() )
            {
                wxCFRef<HIMutableShapeRef> hishape( HIShapeCreateMutableCopy( m_clipRgn ) );
                // if the shape is empty, HIShapeReplacePathInCGContext doesn't work
                if ( HIShapeIsEmpty(hishape))
                {
                    CGRect empty = CGRectMake( 0,0,0,0 );
                    CGContextClipToRect( m_cgContext, empty );
                }
                else
                {
                    HIShapeReplacePathInCGContext( hishape, m_cgContext );
                    CGContextClip( m_cgContext );
                }
            }
#endif
            CGContextConcatCTM( m_cgContext, m_windowTransform );
            CGContextSetTextMatrix( m_cgContext, CGAffineTransformIdentity );
            m_contextSynthesized = true;
            CGContextSaveGState( m_cgContext );

#if 0 // turn on for debugging of clientdc
            static float color = 0.5 ;
            static int channel = 0 ;
            CGRect bounds = CGRectMake(-1000,-1000,2000,2000);
            CGContextSetRGBFillColor( m_cgContext, channel == 0 ? color : 0.5 ,
                channel == 1 ? color : 0.5 , channel == 2 ? color : 0.5 , 1 );
            CGContextFillRect( m_cgContext, bounds );
            color += 0.1 ;
            if ( color > 0.9 )
            {
                color = 0.5 ;
                channel++ ;
                if ( channel == 3 )
                    channel = 0 ;
            }
#endif
        }
    }
    CheckInvariants();

    return m_cgContext != NULL;
}

bool wxMacCoreGraphicsContext::SetAntialiasMode(wxAntialiasMode antialias)
{
    if (!EnsureIsValid())
        return true;

    if (m_antialias == antialias)
        return true;

    m_antialias = antialias;

    if ( !DoSetAntialiasMode(antialias) )
    {
        return false;
    }
    CheckInvariants();
    return true;
}

bool wxMacCoreGraphicsContext::DoSetAntialiasMode(wxAntialiasMode antialias)
{
    bool antialiasMode;
    switch (antialias)
    {
        case wxANTIALIAS_DEFAULT:
            antialiasMode = true;
            break;
        case wxANTIALIAS_NONE:
            antialiasMode = false;
            break;
        default:
            return false;
    }
    CGContextSetShouldAntialias(m_cgContext, antialiasMode);
    return true;
}

bool wxMacCoreGraphicsContext::SetInterpolationQuality(wxInterpolationQuality interpolation)
{
    if (!EnsureIsValid())
        return true;

    if (m_interpolation == interpolation)
        return true;

    m_interpolation = interpolation;

    if ( !DoSetInterpolationQuality(interpolation) )
    {
        return false;
    }
    CheckInvariants();
    return true;
}

bool wxMacCoreGraphicsContext::DoSetInterpolationQuality(wxInterpolationQuality interpolation)
{
    CGInterpolationQuality quality;

    switch (interpolation)
    {
        case wxINTERPOLATION_DEFAULT:
            quality = kCGInterpolationDefault;
            break;
        case wxINTERPOLATION_NONE:
            quality = kCGInterpolationNone;
            break;
        case wxINTERPOLATION_FAST:
            quality = kCGInterpolationLow;
            break;
        case wxINTERPOLATION_GOOD:
            quality = kCGInterpolationMedium;
            break;
        case wxINTERPOLATION_BEST:
            quality = kCGInterpolationHigh;
            break;
        default:
            return false;
    }
    CGContextSetInterpolationQuality(m_cgContext, quality);
    return true;
}

bool wxMacCoreGraphicsContext::SetCompositionMode(wxCompositionMode op)
{
    if (!EnsureIsValid())
        return true;

    if ( m_composition == op )
        return true;

    m_composition = op;

    if ( !DoSetCompositionMode(op) )
    {
        return false;
    }
    CheckInvariants();
    return true;
}

bool wxMacCoreGraphicsContext::DoSetCompositionMode(wxCompositionMode op)
{
    if (op == wxCOMPOSITION_DEST)
        return true;

    // TODO REMOVE if we don't need it because of bugs in 10.5
#if 0
    {
        CGCompositeOperation cop = kCGCompositeOperationSourceOver;
        CGBlendMode mode = kCGBlendModeNormal;
        switch( op )
        {
            case wxCOMPOSITION_CLEAR:
                cop = kCGCompositeOperationClear;
                break;
            case wxCOMPOSITION_SOURCE:
                cop = kCGCompositeOperationCopy;
                break;
            case wxCOMPOSITION_OVER:
                mode = kCGBlendModeNormal;
                break;
            case wxCOMPOSITION_IN:
                cop = kCGCompositeOperationSourceIn;
                break;
            case wxCOMPOSITION_OUT:
                cop = kCGCompositeOperationSourceOut;
                break;
            case wxCOMPOSITION_ATOP:
                cop = kCGCompositeOperationSourceAtop;
                break;
            case wxCOMPOSITION_DEST_OVER:
                cop = kCGCompositeOperationDestinationOver;
                break;
            case wxCOMPOSITION_DEST_IN:
                cop = kCGCompositeOperationDestinationIn;
                break;
            case wxCOMPOSITION_DEST_OUT:
                cop = kCGCompositeOperationDestinationOut;
                break;
            case wxCOMPOSITION_DEST_ATOP:
                cop = kCGCompositeOperationDestinationAtop;
                break;
            case wxCOMPOSITION_XOR:
                cop = kCGCompositeOperationXOR;
                break;
            case wxCOMPOSITION_ADD:
                mode = kCGBlendModePlusLighter ;
                break;
            default:
                return false;
        }
        if ( cop != kCGCompositeOperationSourceOver )
            CGContextSetCompositeOperation(m_cgContext, cop);
        else
            CGContextSetBlendMode(m_cgContext, mode);
    }
#endif
    {
        CGBlendMode mode = kCGBlendModeNormal;
        switch( op )
        {
            case wxCOMPOSITION_CLEAR:
                mode = kCGBlendModeClear;
                break;
            case wxCOMPOSITION_SOURCE:
                mode = kCGBlendModeCopy;
                break;
            case wxCOMPOSITION_OVER:
                mode = kCGBlendModeNormal;
                break;
            case wxCOMPOSITION_IN:
                mode = kCGBlendModeSourceIn;
                break;
            case wxCOMPOSITION_OUT:
                mode = kCGBlendModeSourceOut;
                break;
            case wxCOMPOSITION_ATOP:
                mode = kCGBlendModeSourceAtop;
                break;
            case wxCOMPOSITION_DEST_OVER:
                mode = kCGBlendModeDestinationOver;
                break;
            case wxCOMPOSITION_DEST_IN:
                mode = kCGBlendModeDestinationIn;
                break;
            case wxCOMPOSITION_DEST_OUT:
                mode = kCGBlendModeDestinationOut;
                break;
            case wxCOMPOSITION_DEST_ATOP:
                mode = kCGBlendModeDestinationAtop;
                break;
            case wxCOMPOSITION_XOR:
                mode = kCGBlendModeExclusion; // Not kCGBlendModeXOR!
                break;
            case wxCOMPOSITION_ADD:
                mode = kCGBlendModePlusLighter ;
                break;
            default:
                return false;
        }
        CGContextSetBlendMode(m_cgContext, mode);
    }
    return true;
}

void wxMacCoreGraphicsContext::BeginLayer(wxDouble opacity)
{
    CheckInvariants();
    CGContextSaveGState(m_cgContext);
    CGContextSetAlpha(m_cgContext, (CGFloat) opacity);
    CGContextBeginTransparencyLayer(m_cgContext, 0);
    CheckInvariants();
}

void wxMacCoreGraphicsContext::EndLayer()
{
    CheckInvariants();
    CGContextEndTransparencyLayer(m_cgContext);
    CGContextRestoreGState(m_cgContext);
    CheckInvariants();
}

void wxMacCoreGraphicsContext::Clip( const wxRegion &region )
{
    CheckInvariants();
#if wxOSX_USE_COCOA_OR_CARBON
    if( m_cgContext )
    {
        wxCFRef<HIShapeRef> shape = wxCFRefFromGet(region.GetWXHRGN());
        // if the shape is empty, HIShapeReplacePathInCGContext doesn't work
        if ( HIShapeIsEmpty(shape))
        {
            CGRect empty = CGRectMake( 0,0,0,0 );
            CGContextClipToRect( m_cgContext, empty );
        }
        else
        {
            HIShapeReplacePathInCGContext( shape, m_cgContext );
            CGContextClip( m_cgContext );
        }
    }
    else
    {
        // this offsetting to device coords is not really correct, but since we cannot apply affine transforms
        // to regions we try at least to have correct translations
        HIMutableShapeRef mutableShape = HIShapeCreateMutableCopy( region.GetWXHRGN() );

        CGPoint transformedOrigin = CGPointApplyAffineTransform( CGPointZero, m_windowTransform );
        HIShapeOffset( mutableShape, transformedOrigin.x, transformedOrigin.y );
        m_clipRgn.reset(mutableShape);
    }
#else
    // allow usage as measuring context
    // wxASSERT_MSG( m_cgContext != NULL, "Needs a valid context for clipping" );
#endif
    CheckInvariants();
}

// clips drawings to the rect
void wxMacCoreGraphicsContext::Clip( wxDouble x, wxDouble y, wxDouble w, wxDouble h )
{
    CheckInvariants();
    CGRect r = CGRectMake( (CGFloat) x , (CGFloat) y , (CGFloat) w , (CGFloat) h );
    if ( m_cgContext )
    {
        CGContextClipToRect( m_cgContext, r );
    }
    else
    {
#if wxOSX_USE_COCOA_OR_CARBON
        // the clipping itself must be stored as device coordinates, otherwise
        // we cannot apply it back correctly
        r.origin= CGPointApplyAffineTransform( r.origin, m_windowTransform );
        r.size= CGSizeApplyAffineTransform(r.size, m_windowTransform);
        m_clipRgn.reset(HIShapeCreateWithRect(&r));
#else
    // allow usage as measuring context
    // wxFAIL_MSG( "Needs a valid context for clipping" );
#endif
    }
    CheckInvariants();
}

    // resets the clipping to original extent
void wxMacCoreGraphicsContext::ResetClip()
{
    if ( m_cgContext )
    {
        // there is no way for clearing the clip, we can only revert to the stored
        // state, but then we have to make sure everything else is NOT restored
        CGAffineTransform transform = CGContextGetCTM( m_cgContext );
        CGContextRestoreGState( m_cgContext );
        CGContextSaveGState( m_cgContext );
        CGAffineTransform transformNew = CGContextGetCTM( m_cgContext );
        transformNew = CGAffineTransformInvert( transformNew ) ;
        CGContextConcatCTM( m_cgContext, transformNew);
        CGContextConcatCTM( m_cgContext, transform);
        // Retain antialiasing mode
        DoSetAntialiasMode(m_antialias);
        // Retain interpolation quality
        DoSetInterpolationQuality(m_interpolation);
        // Retain composition mode
        DoSetCompositionMode(m_composition);
    }
    else
    {
#if wxOSX_USE_COCOA_OR_CARBON
        m_clipRgn.reset();
#else
    // allow usage as measuring context
    // wxFAIL_MSG( "Needs a valid context for clipping" );
#endif
    }
    CheckInvariants();
}

void wxMacCoreGraphicsContext::GetClipBox(wxDouble* x, wxDouble* y, wxDouble* w, wxDouble* h)
{
    CGRect r;

    if ( m_cgContext )
    {
        r = CGContextGetClipBoundingBox(m_cgContext);
    }
    else
    {
#if wxOSX_USE_COCOA_OR_CARBON
        HIShapeGetBounds(m_clipRgn, &r);
#else
        r = CGRectMake(0, 0, 0, 0);
    // allow usage as measuring context
    // wxFAIL_MSG( "Needs a valid context for clipping" );
#endif
    }

    if ( CGRectIsEmpty(r) )
    {
        r = CGRectZero;
    }
    CheckInvariants();

    if ( x )
        *x = r.origin.x;
    if ( y )
        *y = r.origin.y;
    if ( w )
        *w = r.size.width;
    if ( h )
        *h = r.size.height;
}

void wxMacCoreGraphicsContext::StrokePath( const wxGraphicsPath &path )
{
    if ( m_pen.IsNull() )
        return ;

    if (!EnsureIsValid())
        return;

    if (m_composition == wxCOMPOSITION_DEST)
        return;

    wxQuartzOffsetHelper helper( m_cgContext , ShouldOffset() );

    ((wxMacCoreGraphicsPenData*)m_pen.GetRefData())->Apply(this);
    CGContextAddPath( m_cgContext , (CGPathRef) path.GetNativePath() );
    CGContextStrokePath( m_cgContext );

    CheckInvariants();
}

void wxMacCoreGraphicsContext::DrawPath( const wxGraphicsPath &path , wxPolygonFillMode fillStyle )
{
    if (!EnsureIsValid())
        return;

    if (m_composition == wxCOMPOSITION_DEST)
        return;

    if ( !m_brush.IsNull() && ((wxMacCoreGraphicsBrushData*)m_brush.GetRefData())->IsShading() )
    {
        // when using shading, we cannot draw pen and brush at the same time
        // revert to the base implementation of first filling and then stroking
        wxGraphicsContext::DrawPath( path, fillStyle );
        return;
    }

    CGPathDrawingMode mode = kCGPathFill ;
    if ( m_brush.IsNull() )
    {
        if ( m_pen.IsNull() )
            return;
        else
            mode = kCGPathStroke;
    }
    else
    {
        if ( m_pen.IsNull() )
        {
            if ( fillStyle == wxODDEVEN_RULE )
                mode = kCGPathEOFill;
            else
                mode = kCGPathFill;
        }
        else
        {
            if ( fillStyle == wxODDEVEN_RULE )
                mode = kCGPathEOFillStroke;
            else
                mode = kCGPathFillStroke;
        }
    }

    if ( !m_brush.IsNull() )
        ((wxMacCoreGraphicsBrushData*)m_brush.GetRefData())->Apply(this);
    if ( !m_pen.IsNull() )
        ((wxMacCoreGraphicsPenData*)m_pen.GetRefData())->Apply(this);

    wxQuartzOffsetHelper helper( m_cgContext , ShouldOffset() );

    CGContextAddPath( m_cgContext , (CGPathRef) path.GetNativePath() );
    CGContextDrawPath( m_cgContext , mode );

    CheckInvariants();
}

void wxMacCoreGraphicsContext::FillPath( const wxGraphicsPath &path , wxPolygonFillMode fillStyle )
{
    if ( m_brush.IsNull() )
        return;

    if (!EnsureIsValid())
        return;

    if (m_composition == wxCOMPOSITION_DEST)
        return;

    if ( ((wxMacCoreGraphicsBrushData*)m_brush.GetRefData())->IsShading() )
    {
        CGContextSaveGState( m_cgContext );
        CGContextAddPath( m_cgContext , (CGPathRef) path.GetNativePath() );
        CGContextClip( m_cgContext );
		if(((wxMacCoreGraphicsBrushData*)m_brush.GetRefData())->IsTransform())
			CGContextConcatCTM( m_cgContext, *((wxMacCoreGraphicsBrushData*)m_brush.GetRefData())->GetTransform());
        CGContextDrawShading( m_cgContext, ((wxMacCoreGraphicsBrushData*)m_brush.GetRefData())->GetShading() );
        CGContextRestoreGState( m_cgContext);
    }
    else
    {
		wxMacCoreGraphicsBrushData* brush = (wxMacCoreGraphicsBrushData*)m_brush.GetRefData();
		if(brush->IsImageFill() && !brush->IsTransform())
		{
			CGAffineTransform contextTransform = ( m_cgContext == NULL ? m_windowTransform : CGContextGetCTM( m_cgContext ));
			brush->SetScaleContext(std::fabs((CGFloat) contextTransform.a) , std::fabs((CGFloat) contextTransform.d ));
		}
		brush->Apply(this);
        CGContextAddPath( m_cgContext , (CGPathRef) path.GetNativePath() );
        if ( fillStyle == wxODDEVEN_RULE )
            CGContextEOFillPath( m_cgContext );
        else
            CGContextFillPath( m_cgContext );
    }

    CheckInvariants();
}

void wxMacCoreGraphicsContext::SetNativeContext( CGContextRef cg )
{
    // we allow either setting or clearing but not replacing
    wxASSERT( m_cgContext == NULL || cg == NULL );

    if ( m_cgContext )
    {
        CheckInvariants();
        CGContextRestoreGState( m_cgContext );
        CGContextRestoreGState( m_cgContext );
        if ( m_contextSynthesized )
        {
#if wxOSX_USE_COCOA
            wxOSXUnlockFocus(m_view);
#endif
        }
        else
            CGContextRelease(m_cgContext);
    }

    m_cgContext = cg;

    // FIXME: This check is needed because currently we need to use a DC/GraphicsContext
    // in order to get font properties, like wxFont::GetPixelSize, but since we don't have
    // a native window attached to use, I create a wxGraphicsContext with a NULL CGContextRef
    // for this one operation.

    // When wxFont::GetPixelSize on Mac no longer needs a graphics context, this check
    // can be removed.
    if (m_cgContext)
    {
        CGContextRetain(m_cgContext);
        CGContextSaveGState( m_cgContext );
        CGContextSetTextMatrix( m_cgContext, CGAffineTransformIdentity );
        CGContextSaveGState( m_cgContext );
        m_contextSynthesized = false;
    }
}

void wxMacCoreGraphicsContext::Translate( wxDouble dx , wxDouble dy )
{
    if ( m_cgContext )
        CGContextTranslateCTM( m_cgContext, (CGFloat) dx, (CGFloat) dy );
    else
        m_windowTransform = CGAffineTransformTranslate(m_windowTransform, (CGFloat) dx, (CGFloat) dy);
}

void wxMacCoreGraphicsContext::Scale( wxDouble xScale , wxDouble yScale )
{
    if ( m_cgContext )
        CGContextScaleCTM( m_cgContext , (CGFloat) xScale , (CGFloat) yScale );
    else
        m_windowTransform = CGAffineTransformScale(m_windowTransform, (CGFloat) xScale, (CGFloat) yScale);
}

void wxMacCoreGraphicsContext::Rotate( wxDouble angle )
{
    if ( m_cgContext )
        CGContextRotateCTM( m_cgContext , (CGFloat) angle );
    else
        m_windowTransform = CGAffineTransformRotate(m_windowTransform, (CGFloat) angle);
}

void wxMacCoreGraphicsContext::DrawBitmap( const wxBitmap &bmp, wxDouble x, wxDouble y, wxDouble w, wxDouble h )
{
#if wxOSX_USE_COCOA
    if (EnsureIsValid())
    {
        CGRect r = CGRectMake( (CGFloat) x , (CGFloat) y , (CGFloat) w , (CGFloat) h );
        wxOSXDrawNSImage( m_cgContext, &r, bmp.GetImage());
    }
#else
    wxGraphicsBitmap bitmap = GetRenderer()->CreateBitmap(bmp);
    DrawBitmap(bitmap, x, y, w, h);
#endif
}

void wxMacCoreGraphicsContext::DrawBitmap( const wxGraphicsBitmap &bmp, wxDouble x, wxDouble y, wxDouble w, wxDouble h )
{
#if wxOSX_USE_COCOA
	{
		CGRect r = CGRectMake( (CGFloat) x , (CGFloat) y , (CGFloat) w , (CGFloat) h );
		const wxGraphicsBitmapData* pData = bmp.GetBitmapData();
		wxOSXDrawNSImage( m_cgContext, &r, pData->GetImage());
	}
#else
	wxGraphicsBitmap bitmap = GetRenderer()->CreateBitmap(bmp);
	DrawBitmap(bitmap, x, y, w, h);
#endif
}

void wxMacCoreGraphicsContext::DrawIcon( const wxIcon &icon, wxDouble x, wxDouble y, wxDouble w, wxDouble h )
{
    if (!EnsureIsValid())
        return;

    if (m_composition == wxCOMPOSITION_DEST)
        return;

#if wxOSX_USE_COCOA
    {
        CGRect r = CGRectMake( (CGFloat) x , (CGFloat) y , (CGFloat) w , (CGFloat) h );
        wxOSXDrawNSImage( m_cgContext, &r, icon.GetImage());
    }
#endif

    CheckInvariants();
}

void wxMacCoreGraphicsContext::PushState()
{
    if (!EnsureIsValid())
        return;

    CGContextSaveGState( m_cgContext );
}

void wxMacCoreGraphicsContext::PopState()
{
    if (!EnsureIsValid())
        return;

    CGContextRestoreGState( m_cgContext );
}

void wxMacCoreGraphicsContext::DoDrawText( const wxString &str, wxDouble x, wxDouble y )
{
    wxCHECK_RET( !m_font.IsNull(), wxT("wxMacCoreGraphicsContext::DrawText - no valid font set") );

    if (!EnsureIsValid())
        return;

    if (m_composition == wxCOMPOSITION_DEST)
        return;

    wxMacCoreGraphicsFontData* fref = (wxMacCoreGraphicsFontData*)m_font.GetRefData();
    wxCFStringRef text(str, wxLocale::GetSystemEncoding() );
    CGColorRef col = wxMacCreateCGColor( fref->GetColour() );
    CTFontRef font = fref->OSXGetCTFont();

    wxCFDictionaryRef fontattr(wxCFRetain(fref->OSXGetCTFontAttributes()));
    wxCFMutableDictionaryRef inlinefontattr;

    bool setColorsInLine = false;

    // if we emulate boldness the stroke color is not taken from the current context
    // therefore we have to set it explicitly
    if ( fontattr.GetValue(kCTStrokeWidthAttributeName) != NULL)
    {
        setColorsInLine = true;
        inlinefontattr = fontattr.CreateMutableCopy();
        inlinefontattr.SetValue(kCTForegroundColorFromContextAttributeName, kCFBooleanFalse);
        inlinefontattr.SetValue(kCTForegroundColorAttributeName,col);
        inlinefontattr.SetValue(kCTStrokeColorAttributeName,col);
    }

    wxCFRef<CFAttributedStringRef> attrtext( CFAttributedStringCreate(kCFAllocatorDefault, text, setColorsInLine ? inlinefontattr : fontattr ) );
    wxCFRef<CTLineRef> line( CTLineCreateWithAttributedString(attrtext) );

    y += CTFontGetAscent(font);

    CGContextSaveGState(m_cgContext);
    CGAffineTransform textMatrix = CGContextGetTextMatrix(m_cgContext);

    CGContextTranslateCTM(m_cgContext, (CGFloat) x, (CGFloat) y);
    CGContextScaleCTM(m_cgContext, 1, -1);
    CGContextSetTextMatrix(m_cgContext, CGAffineTransformIdentity);

    CGContextSetFillColorWithColor( m_cgContext, col );
    CTLineDraw( line, m_cgContext );

    if ( fref->GetUnderlined() ) {
        //AKT: draw horizontal line 1 pixel thick and with 1 pixel gap under baseline
        CGFloat width = CTLineGetTypographicBounds(line, NULL, NULL, NULL);

        CGPoint points[] = { {0.0, -2.0},  {width, -2.0} };

        CGContextSetStrokeColorWithColor(m_cgContext, col);
        CGContextSetShouldAntialias(m_cgContext, false);
        CGContextSetLineWidth(m_cgContext, 1.0);
        CGContextStrokeLineSegments(m_cgContext, points, 2);
    }
    if ( fref->GetStrikethrough() )
    {
        CGFloat width = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
        CGFloat height = CTFontGetXHeight( font ) * 0.6;
        CGPoint points[] = { {0.0, height},  {width, height} };
        CGContextSetStrokeColorWithColor(m_cgContext, col);
        CGContextSetShouldAntialias(m_cgContext, false);
        CGContextSetLineWidth(m_cgContext, 1.0);
        CGContextStrokeLineSegments(m_cgContext, points, 2);
    }

    CGContextRestoreGState(m_cgContext);
    CGContextSetTextMatrix(m_cgContext, textMatrix);
    CGColorRelease( col );
    CheckInvariants();
}

void wxMacCoreGraphicsContext::DoDrawRotatedText(const wxString &str,
                                                 wxDouble x, wxDouble y,
                                                 wxDouble angle)
{
    wxCHECK_RET( !m_font.IsNull(), wxT("wxMacCoreGraphicsContext::DrawText - no valid font set") );

    if (!EnsureIsValid())
        return;

    if (m_composition == wxCOMPOSITION_DEST)
        return;

    // default implementation takes care of rotation and calls non rotated DrawText afterwards
    wxGraphicsContext::DoDrawRotatedText( str, x, y, angle );

    CheckInvariants();
}

void wxMacCoreGraphicsContext::GetTextExtent( const wxString &str, wxDouble *width, wxDouble *height,
                            wxDouble *descent, wxDouble *externalLeading ) const
{
    wxCHECK_RET( !m_font.IsNull(), wxT("wxMacCoreGraphicsContext::GetTextExtent - no valid font set") );

    if ( width )
        *width = 0;
    if ( height )
        *height = 0;
    if ( descent )
        *descent = 0;
    if ( externalLeading )
        *externalLeading = 0;

    // In wxWidgets (MSW-inspired) API it is possible to call GetTextExtent()
    // with an empty string to get just the descent and the leading of the
    // font, so support this (mis)use.
    wxString strToMeasure(str);
    if (str.empty())
        strToMeasure = wxS(" ");

    wxMacCoreGraphicsFontData* fref = (wxMacCoreGraphicsFontData*)m_font.GetRefData();

    wxCFStringRef text(strToMeasure, wxLocale::GetSystemEncoding() );

    wxCFRef<CFAttributedStringRef> attrtext( CFAttributedStringCreate(kCFAllocatorDefault, text, fref->OSXGetCTFontAttributes() ) );
    wxCFRef<CTLineRef> line( CTLineCreateWithAttributedString(attrtext) );

    CGFloat a, d, l, w;
    w = CTLineGetTypographicBounds(line, &a, &d, &l);

    if ( !str.empty() )
    {
        if ( width )
            *width = w;
        if ( height )
            *height = a+d+l;
    }
    if ( descent )
        *descent = d;
    if ( externalLeading )
        *externalLeading = l;

    CheckInvariants();
}

void wxMacCoreGraphicsContext::GetPartialTextExtents(const wxString& text, wxArrayDouble& widths) const
{
    widths.clear();

    wxCHECK_RET( !m_font.IsNull(), wxT("wxMacCoreGraphicsContext::DrawText - no valid font set") );

    if (text.empty())
        return;

    wxMacCoreGraphicsFontData* fref = (wxMacCoreGraphicsFontData*)m_font.GetRefData();

    wxCFStringRef t(text, wxLocale::GetSystemEncoding() );
    wxCFRef<CFAttributedStringRef> attrtext( CFAttributedStringCreate(kCFAllocatorDefault, t, fref->OSXGetCTFontAttributes()) );
    wxCFRef<CTLineRef> line( CTLineCreateWithAttributedString(attrtext) );

    widths.reserve(text.length());
    CFIndex u16index = 1;
    for ( wxString::const_iterator iter = text.begin(); iter != text.end(); ++iter, ++u16index )
    {
        // Take care of surrogate pairs: they take two, not one, of UTF-16 code
        // units used by CoreText.
        if ( *iter >= 0x10000 )
        {
            ++u16index;
        }
        widths.push_back( CTLineGetOffsetForStringIndex( line, u16index, NULL ) );
    }

    CheckInvariants();
}

void * wxMacCoreGraphicsContext::GetNativeContext()
{
    return m_cgContext;
}

void wxMacCoreGraphicsContext::ClearRectangle( wxDouble x, wxDouble y, wxDouble w, wxDouble h )
{
    if (!EnsureIsValid())
        return;

    CGRect rect = CGRectMake( (CGFloat) x , (CGFloat) y , (CGFloat) w , (CGFloat) h );
    CGContextClearRect(m_cgContext, rect);
}

void wxMacCoreGraphicsContext::DrawRectangle( wxDouble x, wxDouble y, wxDouble w, wxDouble h )
{
    if (!EnsureIsValid())
        return;

    if (m_composition == wxCOMPOSITION_DEST)
        return;

    // when using shading, we have to go back to drawing paths
    if ( !m_brush.IsNull() && ((wxMacCoreGraphicsBrushData*)m_brush.GetRefData())->IsShading() )
    {
        wxGraphicsContext::DrawRectangle( x,y,w,h );
        return;
    }

    CGRect rect = CGRectMake( (CGFloat) x , (CGFloat) y , (CGFloat) w , (CGFloat) h );
    if ( !m_brush.IsNull() )
    {
        ((wxMacCoreGraphicsBrushData*)m_brush.GetRefData())->Apply(this);
        CGContextFillRect(m_cgContext, rect);
    }

    wxQuartzOffsetHelper helper( m_cgContext , ShouldOffset() );
    if ( !m_pen.IsNull() )
    {
        ((wxMacCoreGraphicsPenData*)m_pen.GetRefData())->Apply(this);
        CGContextStrokeRect(m_cgContext, rect);
    }
}

// concatenates this transform with the current transform of this context
void wxMacCoreGraphicsContext::ConcatTransform( const wxGraphicsMatrix& matrix )
{
    if ( m_cgContext )
        CGContextConcatCTM( m_cgContext, *(CGAffineTransform*) matrix.GetNativeMatrix());
    else
        m_windowTransform = CGAffineTransformConcat(*(CGAffineTransform*) matrix.GetNativeMatrix(), m_windowTransform);
}

// sets the transform of this context
void wxMacCoreGraphicsContext::SetTransform( const wxGraphicsMatrix& matrix )
{
    CheckInvariants();
    CGAffineTransform t = *((CGAffineTransform*)matrix.GetNativeMatrix());
    if ( m_cgContext )
    {
        CGAffineTransform transform = CGContextGetCTM( m_cgContext );
        transform = CGAffineTransformInvert( transform ) ;
        CGContextConcatCTM( m_cgContext, transform);
        CGContextConcatCTM(m_cgContext, m_initTransform);
        CGContextConcatCTM(m_cgContext, t);
    }
    else
    {
        m_windowTransform = CGAffineTransformConcat(t, m_initTransform);
    }
    CheckInvariants();
}

// gets the matrix of this context
wxGraphicsMatrix wxMacCoreGraphicsContext::GetTransform() const
{
    wxGraphicsMatrix m = CreateMatrix();
    CGAffineTransform* transformMatrix = (CGAffineTransform*)m.GetNativeMatrix();

    if ( m_cgContext )
    {
        *transformMatrix = CGContextGetCTM(m_cgContext);
    }
    else
    {
        *transformMatrix = m_windowTransform;
    }
    // Don't expose internal transformations.
    CGAffineTransform initTransformInv = m_initTransform;
    initTransformInv = CGAffineTransformInvert(initTransformInv);
    *transformMatrix = CGAffineTransformConcat(*transformMatrix, initTransformInv);

    return m;
}

void wxMacCoreGraphicsContext::DrawBitmap(const wxGraphicsBitmap &bmp, const wxRect2DDouble &rcSrc, const wxRect2DDouble &rcDest)
{ 
	if (!EnsureIsValid())
		return;
	//if (m_composition == wxCOMPOSITION_DEST) return;
	
	wxMacCoreGraphicsBitmapData* refdata = static_cast<wxMacCoreGraphicsBitmapData*>(bmp.GetRefData());
	CGImageRef image = static_cast<CGImageRef>(refdata->GetNativeBitmap());
	CGRect cgSrc = CGRectMake( (CGFloat) rcSrc.m_x , (CGFloat) rcSrc.m_y , rcSrc.m_width , rcSrc.m_height );
	CGRect cgDest = CGRectMake( (CGFloat) rcDest.m_x, (CGFloat) rcDest.m_y, rcDest.m_width , rcDest.m_height );
	CGImageRef cgSubimage = CGImageCreateWithImageInRect (image, cgSrc);
	if (!m_brush.IsNull() )
	{
		if ( ((wxMacCoreGraphicsBrushData*)m_brush.GetRefData())->IsShading() )
		{
			
		}
		else
		{
			((wxMacCoreGraphicsBrushData*)m_brush.GetRefData())->Apply(this);
			wxMacDrawCGImage( m_cgContext , &cgDest , cgSubimage );
		}
	}
	else
	{
		wxMacDrawCGImage( m_cgContext , &cgDest , cgSubimage );
	}
	CGImageRelease(cgSubimage);
	CGImageRelease(image);
}

wxMacCoreGraphicsContext::wxMacCoreGraphicsContext(wxGraphicsRenderer* renderer, wxGraphicsBitmap &bitmap) : wxGraphicsContext(renderer)
{ 
	Init();
	m_initTransform = m_windowTransform = CGAffineTransformIdentity;
	if (!bitmap.IsNull())
	{
		bitmap.UnShare();
		wxGraphicsBitmapData* pData = bitmap.GetBitmapData();
		pData->BeginRawAccess() ;
		CGColorSpaceRef genericColorSpace = wxMacGetGenericRGBColorSpace();
		CGContextRef bmCtx = pData->GetBitmapContext();
		
		if ( bmCtx )
		{
			CGContextSetFillColorSpace( bmCtx, genericColorSpace );
			CGContextSetStrokeColorSpace( bmCtx, genericColorSpace );
			SetNativeContext(bmCtx);
		}
		pData->EndRawAccess();
	}
}




#if wxUSE_IMAGE

// ----------------------------------------------------------------------------
// wxMacCoreGraphicsImageContext
// ----------------------------------------------------------------------------

// This is a GC that can be used to draw on wxImage. In this implementation we
// simply draw on a wxBitmap using wxMemoryDC and then convert it to wxImage in
// the end so it's not especially interesting and exists mainly for
// compatibility with the other platforms.
class wxMacCoreGraphicsImageContext : public wxMacCoreGraphicsContext
{
public:
    wxMacCoreGraphicsImageContext(wxGraphicsRenderer* renderer,
                                  wxImage& image) :
        wxMacCoreGraphicsContext(renderer),
        m_image(image),
        m_bitmap(image),
        m_memDC(m_bitmap)
    {
        SetNativeContext
        (
            (CGContextRef)(m_memDC.GetGraphicsContext()->GetNativeContext())
        );
        m_width = image.GetWidth();
        m_height = image.GetHeight();
    }

    virtual ~wxMacCoreGraphicsImageContext()
    {
        m_memDC.SelectObject(wxNullBitmap);
        m_image = m_bitmap.ConvertToImage();
    }

private:
    wxImage& m_image;
    wxBitmap m_bitmap;
    wxMemoryDC m_memDC;
};

#endif // wxUSE_IMAGE

//
// Renderer
//

//-----------------------------------------------------------------------------
// wxMacCoreGraphicsRenderer declaration
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxMacCoreGraphicsRenderer : public wxGraphicsRenderer
{
public :
    wxMacCoreGraphicsRenderer() {}

    virtual ~wxMacCoreGraphicsRenderer() {}

    // Context

    virtual wxGraphicsContext * CreateContext( const wxWindowDC& dc) wxOVERRIDE;
    virtual wxGraphicsContext * CreateContext( const wxMemoryDC& dc) wxOVERRIDE;
#if wxUSE_PRINTING_ARCHITECTURE
    virtual wxGraphicsContext * CreateContext( const wxPrinterDC& dc) wxOVERRIDE;
#endif

    virtual wxGraphicsContext * CreateContextFromNativeContext( void * context ) wxOVERRIDE;

    virtual wxGraphicsContext * CreateContextFromNativeWindow( void * window ) wxOVERRIDE;

    virtual wxGraphicsContext * CreateContext( wxWindow* window ) wxOVERRIDE;
    
    virtual wxGraphicsContext * CreateContextFromBitmap(wxGraphicsBitmap& image) wxOVERRIDE;

#if wxUSE_IMAGE
    virtual wxGraphicsContext * CreateContextFromImage(wxImage& image) wxOVERRIDE;
#endif // wxUSE_IMAGE

    virtual wxGraphicsContext * CreateMeasuringContext() wxOVERRIDE;

    // Path

    virtual wxGraphicsPath CreatePath() wxOVERRIDE;

    // Matrix

    virtual wxGraphicsMatrix CreateMatrix( wxDouble a=1.0, wxDouble b=0.0, wxDouble c=0.0, wxDouble d=1.0,
        wxDouble tx=0.0, wxDouble ty=0.0) wxOVERRIDE;


    virtual wxGraphicsPen CreatePen(const wxGraphicsPenInfo& info) wxOVERRIDE ;

    virtual wxGraphicsBrush CreateBrush(const wxBrush& brush ) wxOVERRIDE ;
	
	virtual wxGraphicsBrush CreateBrush( wxUint32 nARGB ) wxOVERRIDE;
	
	virtual wxGraphicsBrush CreateBrush(const wxGraphicsBitmap& bmp) wxOVERRIDE ;

    virtual wxGraphicsBrush
    CreateLinearGradientBrush(wxDouble x1, wxDouble y1,
                              wxDouble x2, wxDouble y2,
                              const wxGraphicsGradientStops& stops) wxOVERRIDE;

    virtual wxGraphicsBrush
    CreateRadialGradientBrush(wxDouble xo, wxDouble yo,
                              wxDouble xc, wxDouble yc,
                              wxDouble radius,
                              const wxGraphicsGradientStops& stops) wxOVERRIDE;

   // sets the font
    virtual wxGraphicsFont CreateFont( const wxFont &font , const wxColour &col = *wxBLACK ) wxOVERRIDE ;
    virtual wxGraphicsFont CreateFont(double sizeInPixels,
                                      const wxString& facename,
                                      int flags = wxFONTFLAG_DEFAULT,
                                      const wxColour& col = *wxBLACK) wxOVERRIDE;

    // create a native bitmap representation
    virtual wxGraphicsBitmap CreateBitmap( const wxBitmap &bitmap ) wxOVERRIDE ;
	virtual wxGraphicsBitmap CreateBitmap( int w, int h, wxDouble scale = 1 ) wxOVERRIDE;

#if wxUSE_IMAGE
    virtual wxGraphicsBitmap CreateBitmapFromImage(const wxImage& image) wxOVERRIDE;
    virtual wxImage CreateImageFromBitmap(const wxGraphicsBitmap& bmp) wxOVERRIDE;
#endif // wxUSE_IMAGE

    // create a graphics bitmap from a native bitmap
    virtual wxGraphicsBitmap CreateBitmapFromNativeBitmap( void* bitmap ) wxOVERRIDE;

    // create a native bitmap representation
    virtual wxGraphicsBitmap CreateSubBitmap( const wxGraphicsBitmap &bitmap, wxDouble x, wxDouble y, wxDouble w, wxDouble h  ) wxOVERRIDE ;

    virtual wxString GetName() const wxOVERRIDE;
    virtual void GetVersion(int *major, int *minor, int *micro) const wxOVERRIDE;

private :
    wxDECLARE_DYNAMIC_CLASS_NO_COPY(wxMacCoreGraphicsRenderer);
} ;

//-----------------------------------------------------------------------------
// wxMacCoreGraphicsRenderer implementation
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxMacCoreGraphicsRenderer,wxGraphicsRenderer);

static wxMacCoreGraphicsRenderer gs_MacCoreGraphicsRenderer;

wxGraphicsRenderer* wxGraphicsRenderer::GetDefaultRenderer()
{
    return &gs_MacCoreGraphicsRenderer;
}

wxGraphicsContext * wxMacCoreGraphicsRenderer::CreateContext( const wxWindowDC& dc )
{
    wxWindow* const win = dc.GetWindow();
    wxCHECK_MSG( win, NULL, "Invalid wxWindowDC" );

    const wxSize sz = win->GetSize();

    // having a cgctx being NULL is fine (will be created on demand)
    // this is the case for all wxWindowDCs except wxPaintDC
    CGContextRef cgctx = (CGContextRef)(win->MacGetCGContextRef());
    wxMacCoreGraphicsContext *context =
        new wxMacCoreGraphicsContext( this, cgctx, sz.x, sz.y, win );
    context->SetEnableOffsetFromScaleFactor(dc.GetContentScaleFactor());
    return context;
}

wxGraphicsContext * wxMacCoreGraphicsRenderer::CreateContext( const wxMemoryDC& dc )
{
#ifdef __WXMAC__
    const wxDCImpl* impl = dc.GetImpl();
    wxMemoryDCImpl *mem_impl = wxDynamicCast( impl, wxMemoryDCImpl );
    if (mem_impl)
    {
        int w, h;
        mem_impl->GetSize( &w, &h );
        wxMacCoreGraphicsContext* context = new wxMacCoreGraphicsContext( this,
            (CGContextRef)(mem_impl->GetGraphicsContext()->GetNativeContext()), (wxDouble) w, (wxDouble) h );
        context->SetEnableOffsetFromScaleFactor(dc.GetContentScaleFactor());
        return context;
    }
#endif
    return NULL;
}

#if wxUSE_PRINTING_ARCHITECTURE
wxGraphicsContext * wxMacCoreGraphicsRenderer::CreateContext( const wxPrinterDC& dc )
{
#ifdef __WXMAC__
    const wxDCImpl* impl = dc.GetImpl();
    wxPrinterDCImpl *print_impl = wxDynamicCast( impl, wxPrinterDCImpl );
    if (print_impl)
    {
        int w, h;
        print_impl->GetSize( &w, &h );
        return new wxMacCoreGraphicsContext( this,
            (CGContextRef)(print_impl->GetGraphicsContext()->GetNativeContext()), (wxDouble) w, (wxDouble) h );
    }
#endif
    return NULL;
}
#endif

wxGraphicsContext * wxMacCoreGraphicsRenderer::CreateContextFromNativeContext( void * context )
{
    return new wxMacCoreGraphicsContext(this,(CGContextRef)context);
}

wxGraphicsContext * wxMacCoreGraphicsRenderer::CreateContextFromNativeWindow( void * window )
{
    wxUnusedVar(window);
    return NULL;
}

wxGraphicsContext * wxMacCoreGraphicsRenderer::CreateContext( wxWindow* window )
{
    return new wxMacCoreGraphicsContext(this, window );
}

wxGraphicsContext * wxMacCoreGraphicsRenderer::CreateMeasuringContext()
{
    return new wxMacCoreGraphicsContext(this);
}

#if wxUSE_IMAGE

wxGraphicsContext*
wxMacCoreGraphicsRenderer::CreateContextFromImage(wxImage& image)
{
    return new wxMacCoreGraphicsImageContext(this, image);
}

#endif // wxUSE_IMAGE

// Path

wxGraphicsPath wxMacCoreGraphicsRenderer::CreatePath()
{
    wxGraphicsPath m;
    m.SetRefData( new wxMacCoreGraphicsPathData(this));
    return m;
}


// Matrix

wxGraphicsMatrix wxMacCoreGraphicsRenderer::CreateMatrix( wxDouble a, wxDouble b, wxDouble c, wxDouble d,
    wxDouble tx, wxDouble ty)
{
    wxGraphicsMatrix m;
    wxMacCoreGraphicsMatrixData* data = new wxMacCoreGraphicsMatrixData( this );
    data->Set( a,b,c,d,tx,ty ) ;
    m.SetRefData(data);
    return m;
}

wxGraphicsPen wxMacCoreGraphicsRenderer::CreatePen(const wxGraphicsPenInfo& info)
{
    if ( info.IsTransparent() )
        return wxNullGraphicsPen;
    else
    {
        wxGraphicsPen p;
        p.SetRefData(new wxMacCoreGraphicsPenData( this, info ));
        return p;
    }
}

wxGraphicsBrush wxMacCoreGraphicsRenderer::CreateBrush(const wxBrush& brush )
{
    if ( !brush.IsOk() || brush.GetStyle() == wxBRUSHSTYLE_TRANSPARENT )
        return wxNullGraphicsBrush;
    else
    {
        wxGraphicsBrush p;
        p.SetRefData(new wxMacCoreGraphicsBrushData( this, brush ));
        return p;
    }
}

wxGraphicsBrush wxMacCoreGraphicsRenderer::CreateBrush(const wxGraphicsBitmap& bmp)
{
	wxGraphicsBrush p;
	p.SetRefData(new wxMacCoreGraphicsBrushData( this, bmp ));
	return p;
}

wxGraphicsBitmap wxMacCoreGraphicsRenderer::CreateBitmap( const wxBitmap& bmp )
{
    if ( bmp.IsOk() )
    {
        wxGraphicsBitmap p;
        p.SetRefData(new wxMacCoreGraphicsBitmapData( this , bmp.CreateCGImage() ) );
        return p;
    }
    else
        return wxNullGraphicsBitmap;
}

#if wxUSE_IMAGE

wxGraphicsBitmap
wxMacCoreGraphicsRenderer::CreateBitmapFromImage(const wxImage& image)
{
    // We don't have any direct way to convert wxImage to CGImage so pass by
    // wxBitmap. This makes this function pretty useless in this implementation
    // but it allows to have the same API as with Cairo backend where we can
    // convert wxImage to a Cairo surface directly, bypassing wxBitmap.
    return CreateBitmap(wxBitmap(image));
}

wxImage wxMacCoreGraphicsRenderer::CreateImageFromBitmap(const wxGraphicsBitmap& bmp)
{
    wxMacCoreGraphicsBitmapData* const
        data = static_cast<wxMacCoreGraphicsBitmapData*>(bmp.GetRefData());

    return data ? data->ConvertToImage() : wxNullImage;
}

#endif // wxUSE_IMAGE

wxGraphicsBitmap wxMacCoreGraphicsRenderer::CreateBitmapFromNativeBitmap( void* bitmap )
{
    if ( bitmap != NULL )
    {
        wxGraphicsBitmap p;
        p.SetRefData(new wxMacCoreGraphicsBitmapData( this , (CGImageRef) bitmap ));
        return p;
    }
    else
        return wxNullGraphicsBitmap;
}

wxGraphicsBitmap wxMacCoreGraphicsRenderer::CreateSubBitmap( const wxGraphicsBitmap &bmp, wxDouble x, wxDouble y, wxDouble w, wxDouble h  )
{
    wxMacCoreGraphicsBitmapData* refdata = static_cast<wxMacCoreGraphicsBitmapData*>(bmp.GetRefData());
    CGImageRef img = refdata->CreateCGImage();
    if ( img )
    {
        wxGraphicsBitmap p;
        CGImageRef subimg = CGImageCreateWithImageInRect(img, CGRectMake( (CGFloat) x , (CGFloat) y , (CGFloat) w , (CGFloat) h ));
        p.SetRefData(new wxMacCoreGraphicsBitmapData( this , subimg ) );
        return p;
    }
    else
        return wxNullGraphicsBitmap;
}

wxString wxMacCoreGraphicsRenderer::GetName() const
{
    return "cg";
}

void wxMacCoreGraphicsRenderer::GetVersion(int *major, int *minor, int *micro) const
{
    if ( major )
        *major = wxPlatformInfo::Get().GetOSMajorVersion();
    if ( minor )
        *minor = wxPlatformInfo::Get().GetOSMinorVersion();
    if ( micro )
        *micro = 0;
}

wxGraphicsBrush
wxMacCoreGraphicsRenderer::CreateLinearGradientBrush(wxDouble x1, wxDouble y1,
                                                     wxDouble x2, wxDouble y2,
                                                     const wxGraphicsGradientStops& stops)
{
    wxGraphicsBrush p;
    wxMacCoreGraphicsBrushData* d = new wxMacCoreGraphicsBrushData( this );
    d->CreateLinearGradientBrush(x1, y1, x2, y2, stops);
    p.SetRefData(d);
    return p;
}

wxGraphicsBrush
wxMacCoreGraphicsRenderer::CreateRadialGradientBrush(wxDouble xo, wxDouble yo,
                                                     wxDouble xc, wxDouble yc,
                                                     wxDouble radius,
                                                     const wxGraphicsGradientStops& stops)
{
    wxGraphicsBrush p;
    wxMacCoreGraphicsBrushData* d = new wxMacCoreGraphicsBrushData( this );
    d->CreateRadialGradientBrush(xo, yo, xc, yc, radius, stops);
    p.SetRefData(d);
    return p;
}

wxGraphicsFont wxMacCoreGraphicsRenderer::CreateFont( const wxFont &font , const wxColour &col )
{
    if ( font.IsOk() )
    {
        wxGraphicsFont p;
        p.SetRefData(new wxMacCoreGraphicsFontData( this , font, col ));
        return p;
    }
    else
        return wxNullGraphicsFont;
}

wxGraphicsFont
wxMacCoreGraphicsRenderer::CreateFont(double sizeInPixels,
                                      const wxString& facename,
                                      int flags,
                                      const wxColour& col)
{
    // Notice that under Mac we always use 72 DPI so the font size in pixels is
    // the same as the font size in points and we can pass it directly to wxFont
    // ctor.
    wxFont font(wxFontInfo(sizeInPixels).FaceName(facename).AllFlags(flags));

    wxGraphicsFont f;
    f.SetRefData(new wxMacCoreGraphicsFontData(this, font, col));
    return f;
}

wxGraphicsContext *wxMacCoreGraphicsRenderer::CreateContextFromBitmap(wxGraphicsBitmap &image)
{ 
	return new wxMacCoreGraphicsContext(this, image);
}

wxGraphicsBitmap wxMacCoreGraphicsRenderer::CreateBitmap(int w, int h, wxDouble scale)
{
	if ( w > 0 && h > 0 )
	{
		wxGraphicsBitmap p;
		p.SetRefData(new wxMacCoreGraphicsBitmapData( this, w, h, scale ));
		return p;
	}
	else
		return wxNullGraphicsBitmap;
}

//
// CoreGraphics Helper Methods
//

// Data Providers and Consumers

size_t UMAPutBytesCFRefCallback( void *info, const void *bytes, size_t count )
{
    CFMutableDataRef data = (CFMutableDataRef) info;
    if ( data )
    {
        CFDataAppendBytes( data, (const UInt8*) bytes, count );
    }
    return count;
}

void wxMacReleaseCFDataProviderCallback(void *info,
                                      const void *WXUNUSED(data),
                                      size_t WXUNUSED(count))
{
    if ( info )
        CFRelease( (CFDataRef) info );
}

void wxMacReleaseCFDataConsumerCallback( void *info )
{
    if ( info )
        CFRelease( (CFDataRef) info );
}

CGDataProviderRef wxMacCGDataProviderCreateWithCFData( CFDataRef data )
{
    if ( data == NULL )
        return NULL;

    return CGDataProviderCreateWithCFData( data );
}

CGDataConsumerRef wxMacCGDataConsumerCreateWithCFData( CFMutableDataRef data )
{
    if ( data == NULL )
        return NULL;

    return CGDataConsumerCreateWithCFData( data );
}

void
wxMacReleaseMemoryBufferProviderCallback(void *info,
                                         const void * WXUNUSED_UNLESS_DEBUG(data),
                                         size_t WXUNUSED(size))
{
    wxMemoryBuffer* membuf = (wxMemoryBuffer*) info ;

    wxASSERT( data == membuf->GetData() ) ;

    delete membuf ;
}

CGDataProviderRef wxMacCGDataProviderCreateWithMemoryBuffer( const wxMemoryBuffer& buf )
{
    wxMemoryBuffer* b = new wxMemoryBuffer( buf );
    if ( b->GetDataLen() == 0 )
    {
        delete b;
        return NULL;
    }

    return CGDataProviderCreateWithData( b , (const void *) b->GetData() , b->GetDataLen() ,
                                                 wxMacReleaseMemoryBufferProviderCallback );
}
