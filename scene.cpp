/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

/*
 (NOTE: The compositing code is work in progress. As such this design
 documentation may get outdated in some areas.)

 The base class for compositing, implementing shared functionality
 between the OpenGL and XRender backends.
 
 Design:
 
 When compositing is turned on, XComposite extension is used to redirect
 drawing of windows to pixmaps and XDamage extension is used to get informed
 about damage (changes) to window contents. This code is mostly in composite.cpp .
 
 Workspace::performCompositing() starts one painting pass. Painting is done
 by painting the screen, which in turn paints every window. Painting can be affected
 using effects, which are chained. E.g. painting a screen means that actually
 paintScreen() of the first effect is called, which possibly does modifications
 and calls next effect's paintScreen() and so on, until Scene::finalPaintScreen()
 is called.
 
 There are 3 phases of every paint (not necessarily done together):
 The pre-paint phase, the paint phase and the post-paint phase.
 
 The pre-paint phase is used to find out about how the painting will be actually
 done (i.e. what the effects will do). For example when only a part of the screen
 needs to be updated and no effect will do any transformation it is possible to use
 an optimized paint function. How the painting will be done is controlled
 by the mask argument, see PAINT_WINDOW_* and PAINT_SCREEN_* flags in scene.h .
 For example an effect that decides to paint a normal windows as translucent
 will need to modify the mask in its prePaintWindow() to include
 the PAINT_WINDOW_TRANSLUCENT flag. The paintWindow() function will then get
 the mask with this flag turned on and will also paint using transparency.
 
 The paint pass does the actual painting, based on the information collected
 using the pre-paint pass. After running through the effects' paintScreen()
 either paintGenericScreen() or optimized paintSimpleScreen() are called.
 Those call paintWindow() on windows (not necessarily all), possibly using
 clipping to optimize performance and calling paintWindow() first with only
 PAINT_WINDOW_OPAQUE to paint the opaque parts and then later
 with PAINT_WINDOW_TRANSLUCENT to paint the transparent parts. Function
 paintWindow() again goes through effects' paintWindow() until
 finalPaintWindow() is called, which calls the window's performPaint() to
 do the actual painting.
 
 The post-paint can be used for cleanups and is also used for scheduling
 repaints during the next painting pass for animations. Effects wanting to
 repaint certain parts can manually damage them during post-paint and repaint
 of these parts will be done during the next paint pass.
 
*/

#include "scene.h"

#include <X11/extensions/shape.h>

#include "client.h"
#include "deleted.h"
#include "effects.h"

namespace KWin
{

//****************************************
// Scene
//****************************************

Scene* scene;

Scene::Scene( Workspace* ws )
    : wspace( ws ),
    has_waitSync( false )
    {
    }
    
Scene::~Scene()
    {
    }

// returns mask and possibly modified region
void Scene::paintScreen( int* mask, QRegion* region )
    {
    *mask = ( *region == QRegion( 0, 0, displayWidth(), displayHeight()))
        ? 0 : PAINT_SCREEN_REGION;
    updateTimeDiff();
    // preparation step
    static_cast<EffectsHandlerImpl*>(effects)->startPaint();
    ScreenPrePaintData pdata;
    pdata.mask = *mask;
    pdata.paint = *region;
    effects->prePaintScreen( pdata, time_diff );
    *mask = pdata.mask;
    *region = pdata.paint;
    if( *mask & ( PAINT_SCREEN_TRANSFORMED | PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS ))
        { // Region painting is not possible with transformations,
          // because screen damage doesn't match transformed positions.
        *mask &= ~PAINT_SCREEN_REGION;
        *region = infiniteRegion();
        }
    else if(  *mask & PAINT_SCREEN_REGION )
        { // make sure not to go outside visible screen
        *region &= QRegion( 0, 0, displayWidth(), displayHeight());
        }
    else
        { // whole screen, not transformed, force region to be full
        *region = QRegion( 0, 0, displayWidth(), displayHeight());
        }
    painted_region = *region;
    if( *mask & PAINT_SCREEN_BACKGROUND_FIRST )
        paintBackground( *region );
    ScreenPaintData data;
    effects->paintScreen( *mask, *region, data );
    foreach( Window* w, stacking_order )
        effects->postPaintWindow( effectWindow( w ));
    effects->postPaintScreen();
    *region |= painted_region;
    // make sure not to go outside of the screen area
    *region &= QRegion( 0, 0, displayWidth(), displayHeight());
    }

// Compute time since the last painting pass.
void Scene::updateTimeDiff()
    {
    if( last_time.isNull())
        {
        // Painting has been idle (optimized out) for some time,
        // which means time_diff would be huge and would break animations.
        // Simply set it to one (zero would mean no change at all and could
        // cause problems).
        time_diff = 1;
        }
    else
        time_diff = last_time.elapsed();
    if( time_diff < 0 ) // check time rollback
        time_diff = 1;
    last_time.start();;
    }

// Painting pass is optimized away.
void Scene::idle()
    {
    // Don't break time since last paint for the next pass.
    last_time = QTime();
    }

// the function that'll be eventually called by paintScreen() above
void Scene::finalPaintScreen( int mask, QRegion region, ScreenPaintData& data )
    {
    if( mask & ( PAINT_SCREEN_TRANSFORMED | PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS ))
        paintGenericScreen( mask, data );
    else
        paintSimpleScreen( mask, region );
    }

// The generic painting code that can handle even transformations.
// It simply paints bottom-to-top.
void Scene::paintGenericScreen( int orig_mask, ScreenPaintData )
    {
    if( !( orig_mask & PAINT_SCREEN_BACKGROUND_FIRST ))
        paintBackground( infiniteRegion());
    QList< Phase2Data > phase2;
    foreach( Window* w, stacking_order ) // bottom to top
        {
        WindowPrePaintData data;
        data.mask = orig_mask | ( w->isOpaque() ? PAINT_WINDOW_OPAQUE : PAINT_WINDOW_TRANSLUCENT );
        w->resetPaintingEnabled();
        data.paint = infiniteRegion(); // no clipping, so doesn't really matter
        data.clip = QRegion();
        data.quads = buildQuads( w );
        // preparation step
        effects->prePaintWindow( effectWindow( w ), data, time_diff );
        if( !w->isPaintingEnabled())
            continue;
        phase2.append( Phase2Data( w, infiniteRegion(), data.mask, data.quads ));
        }

    foreach( Phase2Data d, phase2 )
        paintWindow( d.window, d.mask, d.region, d.quads );
    }

// The optimized case without any transformations at all.
// It can paint only the requested region and can use clipping
// to reduce painting and improve performance.
void Scene::paintSimpleScreen( int orig_mask, QRegion region )
    {
    // TODO PAINT_WINDOW_* flags don't belong here, that's why it's in the assert,
    // perhaps the two enums should be separated
    assert(( orig_mask & ( PAINT_WINDOW_TRANSFORMED | PAINT_SCREEN_TRANSFORMED
        | PAINT_WINDOW_TRANSLUCENT | PAINT_WINDOW_OPAQUE )) == 0 );
    QList< Phase2Data > phase2opaque;
    QList< Phase2Data > phase2translucent;
    QRegion allclips;
    // Draw each opaque window top to bottom, subtracting the bounding rect of
    // each window from the clip region after it's been drawn.
    for( int i = stacking_order.count() - 1; // top to bottom
         i >= 0;
         --i )
        {
        Window* w = stacking_order[ i ];
        WindowPrePaintData data;
        data.mask = orig_mask | ( w->isOpaque() ? PAINT_WINDOW_OPAQUE : PAINT_WINDOW_TRANSLUCENT );
        w->resetPaintingEnabled();
        data.paint = region;
        data.clip = w->isOpaque() ? w->shape().translated( w->x(), w->y()) : QRegion();
        data.quads = buildQuads( w );
        // preparation step
        effects->prePaintWindow( effectWindow( w ), data, time_diff );
        if( !w->isPaintingEnabled())
            continue;
        data.paint -= allclips; // make sure to avoid already clipped areas
        if( data.paint.isEmpty()) // completely clipped
            continue;
        if( data.paint != region ) // prepaint added area to draw
            {
            region |= data.paint; // make sure other windows in that area get painted too
            painted_region |= data.paint; // make sure it makes it to the screen
            }
        // If the window is transparent, the transparent part will be done
        // in the 2nd pass.
        if( data.mask & PAINT_WINDOW_TRANSLUCENT )
            phase2translucent.prepend( Phase2Data( w, data.paint, data.mask, data.quads ));
        if( data.mask & PAINT_WINDOW_OPAQUE )
            {
            phase2opaque.append( Phase2Data( w, data.paint, data.mask, data.quads ));
            // The window can clip by its opaque parts the windows below.
            region -= data.clip;
            allclips |= data.clip;
            }
        }
    // Do the actual painting
    // First opaque windows, top to bottom
    foreach( Phase2Data d, phase2opaque )
        paintWindow( d.window, d.mask, d.region, d.quads );
    if( !( orig_mask & PAINT_SCREEN_BACKGROUND_FIRST ))
        paintBackground( region ); // Fill any areas of the root window not covered by windows
    // Now walk the list bottom to top, drawing translucent windows.
    // That we draw bottom to top is important now since we're drawing translucent objects
    // and also are clipping only by opaque windows.
    QRegion add_paint;
    foreach( Phase2Data d, phase2translucent )
        {
        paintWindow( d.window, d.mask, d.region | add_paint, d.quads );
        // It is necessary to also add paint regions of windows below, because their
        // pre-paint's might have extended the paint area, so those areas need to be painted too.
        add_paint |= d.region;
        }
    }

void Scene::paintWindow( Window* w, int mask, QRegion region, WindowQuadList quads )
    {
    WindowPaintData data;
    data.opacity = w->window()->opacity();
    data.quads = quads;
    w->prepareForPainting();
    effects->paintWindow( effectWindow( w ), mask, region, data );
    }

// the function that'll be eventually called by paintWindow() above
void Scene::finalPaintWindow( EffectWindowImpl* w, int mask, QRegion region, WindowPaintData& data )
    {
    effects->drawWindow( w, mask, region, data );
    }

// will be eventually called from drawWindow()
void Scene::finalDrawWindow( EffectWindowImpl* w, int mask, QRegion region, WindowPaintData& data )
    {
    w->sceneWindow()->performPaint( mask, region, data );
    }

WindowQuadList Scene::buildQuads( const Window* w )
    {
    WindowQuadList ret;
    foreach( QRect r, w->shape().rects())
        {
        WindowQuad quad;
        // TODO asi mam spatne pravy dolni roh - bud tady, nebo v jinych castech
        quad[ 0 ] = WindowVertex( r.x(), r.y(), r.x(), r.y());
        quad[ 1 ] = WindowVertex( r.x() + r.width(), r.y(), r.x() + r.width(), r.y());
        quad[ 2 ] = WindowVertex( r.x() + r.width(), r.y() + r.height(), r.x() + r.width(), r.y() + r.height());
        quad[ 3 ] = WindowVertex( r.x(), r.y() + r.height(), r.x(), r.y() + r.height());
        ret.append( quad );
        }
    return ret;
    }

//****************************************
// Scene::Window
//****************************************

Scene::Window::Window( Toplevel * c )
    : toplevel( c )
    , filter( ImageFilterFast )
    , disable_painting( 0 )
    , shape_valid( false )
    {
    }

Scene::Window::~Window()
    {
    }

void Scene::Window::discardShape()
    {
    // it is created on-demand and cached, simply
    // reset the flag
    shape_valid = false;
    }

// Find out the shape of the window using the XShape extension
// or if shape is not set then simply it's the window geometry.
QRegion Scene::Window::shape() const
    {
    if( !shape_valid )
        {
        Client* c = dynamic_cast< Client* >( toplevel );
        if( toplevel->shape() || ( c != NULL && !c->mask().isEmpty()))
            {
            int count, order;
            XRectangle* rects = XShapeGetRectangles( display(), toplevel->frameId(),
                ShapeBounding, &count, &order );
            if(rects)
                {
                shape_region = QRegion();
                for( int i = 0;
                     i < count;
                     ++i )
                    shape_region += QRegion( rects[ i ].x, rects[ i ].y,
                        rects[ i ].width, rects[ i ].height );
                XFree(rects);
                }
            else
                shape_region = QRegion( 0, 0, width(), height());
            }
        else
            shape_region = QRegion( 0, 0, width(), height());
        shape_valid = true;
        }
    return shape_region;
    }

bool Scene::Window::isVisible() const
    {
    if( dynamic_cast< Deleted* >( toplevel ) != NULL )
        return false;
    if( !toplevel->isOnCurrentDesktop())
        return false;
    if( Client* c = dynamic_cast< Client* >( toplevel ))
        return c->isShown( true );
    return true; // Unmanaged is always visible
    // TODO there may be transformations, so ignore this for now
    return !toplevel->geometry()
        .intersect( QRect( 0, 0, displayWidth(), displayHeight()))
        .isEmpty();
    }

bool Scene::Window::isOpaque() const
    {
    return toplevel->opacity() == 1.0 && !toplevel->hasAlpha();
    }

bool Scene::Window::isPaintingEnabled() const
    {
    return !disable_painting;
    }

void Scene::Window::resetPaintingEnabled()
    {
    disable_painting = 0;
    if( dynamic_cast< Deleted* >( toplevel ) != NULL )
        disable_painting |= PAINT_DISABLED_BY_DELETE;
    if( !toplevel->isOnCurrentDesktop())
        disable_painting |= PAINT_DISABLED_BY_DESKTOP;
    if( Client* c = dynamic_cast< Client* >( toplevel ))
        {
        if( c->isMinimized() )
            disable_painting |= PAINT_DISABLED_BY_MINIMIZE;
        }
    }

void Scene::Window::enablePainting( int reason )
    {
    disable_painting &= ~reason;
    }

void Scene::Window::disablePainting( int reason )
    {
    disable_painting |= reason;
    }

} // namespace
