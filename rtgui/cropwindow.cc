/*
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>
 *
 *  RawTherapee is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  RawTherapee is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with RawTherapee.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "cropwindow.h"

#include <iomanip>

#include "../rtengine/mytime.h"
#include "../rtengine/rt_math.h"
#include "../rtengine/dcrop.h"

#include "guiutils.h"
#include "threadutils.h"
#include "rtimage.h"
#include "cursormanager.h"
#include "options.h"
#include "imagearea.h"

using namespace rtengine;

struct ZoomStep {
    Glib::ustring label;
    double zoom;
    int czoom;
};

ZoomStep zoomSteps[] = {
    {"  1%",  0.01,    100},
    {"  2%",  0.02,    50},
    {"  5%",  0.05,    20},
    {"6.7%",  1.0 / 15.0, 15},
    {"  8%",  1.0 / 12.0, 12},
    {" 10%",  0.1,     10},
    {"12.5%", 0.125,   8},
    {" 14%",  1.0 / 7.0, 7},
    {"16.6%", 1.0 / 6.0, 6},
    {" 20%",  0.2,     5},
    {" 25%",  0.25,    4},
    {" 33%",  1.0 / 3.0, 3},
    {" 50%",  0.5,     2},
    {"100%",  1.0,     1000},
    {"200%",  2.0,     2000},
    {"300%",  3.0,     3000},
    {"400%",  4.0,     4000},
    {"500%",  5.0,     5000},
    {"600%",  6.0,     6000},
    {"700%",  7.0,     7000},
    {"800%",  8.0,     8000}
};
#define MAXZOOMSTEPS 20
#define ZOOM11INDEX  13

CropWindow::CropWindow (ImageArea* parent, rtengine::StagedImageProcessor* ipc_, bool isLowUpdatePriority_, bool isDetailWindow)
    : ObjectMOBuffer(parent), state(SNormal), press_x(0), press_y(0), action_x(0), action_y(0), pickedObject(-1), pickModifierKey(0), rot_deg(0), onResizeArea(false), deleted(false),
      fitZoomEnabled(true), fitZoom(false), isLowUpdatePriority(isLowUpdatePriority_), cropLabel(Glib::ustring("100%")),
      backColor(options.bgcolor), decorated(true), isFlawnOver(false), titleHeight(30), sideBorderWidth(3), lowerBorderWidth(3),
      upperBorderWidth(1), sepWidth(2), xpos(30), ypos(30), width(0), height(0), imgAreaX(0), imgAreaY(0), imgAreaW(0), imgAreaH(0),
      imgX(-1), imgY(-1), imgW(1), imgH(1), iarea(parent), cropZoom(0), zoomVersion(0), exposeVersion(0), cropgl(NULL),
      pmlistener(NULL), pmhlistener(NULL), observedCropWin(NULL), ipc(ipc_)
{
    Glib::RefPtr<Pango::Context> context = parent->get_pango_context () ;
    Pango::FontDescription fontd = context->get_font_description ();
    fontd.set_weight (Pango::WEIGHT_BOLD);
    fontd.set_size(8 * Pango::SCALE);
    context->set_font_description (fontd);
    Glib::RefPtr<Pango::Layout> cllayout = parent->create_pango_layout("1000%");

    int iw, ih;
    cllayout->get_pixel_size (iw, ih);

    titleHeight = ih;

    bZoomOut = new LWButton (RTImage::createFromPng ("gtk-zoom-out-small.png"), 0, NULL, LWButton::Left, LWButton::Center, "Zoom Out");
    bZoomIn  = new LWButton (RTImage::createFromPng ("gtk-zoom-in-small.png"),  1, NULL, LWButton::Left, LWButton::Center, "Zoom In");
    bZoom100 = new LWButton (RTImage::createFromPng ("gtk-zoom-100-small.png"), 2, NULL, LWButton::Left, LWButton::Center, "Zoom 100/%");
    //bZoomFit = new LWButton (RTImage::createFromPng ("gtk-zoom-fit.png"), 3, NULL, LWButton::Left, LWButton::Center, "Zoom Fit");
    bClose   = new LWButton (RTImage::createFromPng ("gtk-close-small.png"),    4, NULL, LWButton::Right, LWButton::Center, "Close");

    buttonSet.add (bZoomOut);
    buttonSet.add (bZoomIn);
    buttonSet.add (bZoom100);
    buttonSet.add (bClose);

    buttonSet.setColors (Gdk::Color("black"), Gdk::Color("white"));
    buttonSet.setButtonListener (this);

    int bsw, bsh;
    buttonSet.getMinimalDimensions (bsw, bsh);

    if (bsh > titleHeight) {
        titleHeight = bsh;
    }

    minWidth = bsw + iw + 2 * sideBorderWidth;

    cropHandler.setDisplayHandler(this);
    cropHandler.newImage (ipc_, isDetailWindow);
}

void CropWindow::enable()
{
    cropHandler.setEnabled (true);
}

void CropWindow::setPosition (int x, int y)
{

    if (y < 0) {
        y = 0;
    }

    xpos = x;
    ypos = y;

    if (decorated) {
        buttonSet.arrangeButtons (xpos + sideBorderWidth, ypos + upperBorderWidth, width - 2 * sideBorderWidth, titleHeight);
    }
}

void CropWindow::getPosition (int& x, int& y)
{

    x = xpos;
    y = ypos;
}

void CropWindow::getCropPosition (int& x, int& y)
{

    int cropX, cropY;
    cropHandler.getPosition (cropX, cropY);

    if (state != SCropImgMove) {
        x = cropX;
        y = cropY;
    } else {
        x = cropX + action_x;
        y = cropY + action_y;
    }
}

void CropWindow::getCropRectangle (int& x, int& y, int& w, int& h)
{

    cropHandler.getPosition (x, y);
    cropHandler.getSize (w, h);
}

void CropWindow::setCropPosition (int x, int y, bool update)
{

    cropHandler.setAnchorPosition (x, y, update);

    for (auto listener : listeners) {
        listener->cropPositionChanged (this);
    }
}

void CropWindow::centerCrop (bool update)
{

    cropHandler.centerAnchor (update);

    for (auto listener : listeners) {
        listener->cropPositionChanged (this);
    }
}

void CropWindow::setSize (int w, int h, bool norefresh)
{

    width = w;
    height = h;

    fitZoom = false;

    if (width < minWidth) {
        width = minWidth;
    }

    if (height < 64) {
        height = 64;
    }

    if (decorated) {
        imgAreaX = sideBorderWidth;
        imgAreaY = upperBorderWidth + titleHeight + sepWidth;
        imgAreaW = width - 2 * sideBorderWidth;
        imgAreaH = height - lowerBorderWidth - titleHeight - sepWidth - upperBorderWidth;
        buttonSet.arrangeButtons (xpos + sideBorderWidth, ypos + upperBorderWidth, width - 2 * sideBorderWidth, titleHeight);
    } else {
        imgAreaX = imgAreaY = 0;
        imgAreaW = width;
        imgAreaH = height;
    }

    if (!norefresh) {
        ObjectMOBuffer::resize(imgAreaW, imgAreaH);
        cropHandler.setWSize (imgAreaW, imgAreaH);
    }

    //iarea->redraw ();
}

void CropWindow::getSize (int& w, int& h)
{

    w = width;
    h = height;
}

void CropWindow::getCropSize (int& w, int& h)
{

    w = imgAreaW;
    h = imgAreaH;
}

void CropWindow::getCropAnchorPosition (int& x, int& y)
{
    cropHandler.getAnchorPosition(x, y);
}

void CropWindow::setCropAnchorPosition (int& x, int& y)
{
    cropHandler.setAnchorPosition(x, y);
}

bool CropWindow::isInside (int x, int y)
{

    return x >= xpos && x < xpos + width && y >= ypos && y < ypos + height;
}

void CropWindow::leaveNotify (GdkEventCrossing* event)
{
    EditSubscriber* subscriber = iarea->getCurrSubscriber();

    if (state == SNormal && subscriber && subscriber->getEditingType() == ET_PIPETTE) {
        iarea->pipetteVal[0] = iarea->pipetteVal[1] = iarea->pipetteVal[2] = -1.f;

        if (subscriber->mouseOver(0)) {
            iarea->redraw();
        }
    }
}

void CropWindow::flawnOver (bool isFlawnOver)
{
    this->isFlawnOver = isFlawnOver;
}

void CropWindow::buttonPress (int button, int type, int bstate, int x, int y)
{

    bool needRedraw = true;  // most common case ; not redrawing are exceptions
    const auto editSubscriber = iarea->getCurrSubscriber();

    iarea->grabFocus (this);

    if (button == 1) {
        if (type == GDK_2BUTTON_PRESS && onArea (CropImage, x, y) && (state == SNormal || state == SCropImgMove)) {
            if (fitZoomEnabled) {
                if (fitZoom) {
                    state = SNormal;
                    zoomVersion = exposeVersion;
                    screenCoordToImage (x, y, action_x, action_y);
                    changeZoom (ZOOM11INDEX, true, action_x, action_y);
                    fitZoom = false;
                } else {
                    zoomFit ();
                }
            } else {
                zoom11 ();
            }

            state = SNormal;
        }
        else if (type == GDK_BUTTON_PRESS && state == SNormal) {
            if (onArea (CropToolBar, x, y)) {
                if (!decorated || !buttonSet.pressNotify (x, y)) {
                    state = SCropWinMove;
                    action_x = x;
                    action_y = y;
                    press_x = xpos;
                    press_y = ypos;
                }
            } else if (onArea (CropResize, x, y)) {
                state = SCropWinResize;
                action_x = x;
                action_y = y;
                press_x = width;
                press_y = height;
            } else {
                if (onArea (CropImage, x, y)) {  // events inside of the image domain
                    if (onArea (CropTopLeft, x, y)) {
                        state = SResizeTL;
                        press_x = x;
                        action_x = cropHandler.cropParams.x;
                        press_y = y;
                        action_y = cropHandler.cropParams.y;
                    } else if (onArea (CropTopRight, x, y)) {
                        state = SResizeTR;
                        press_x = x;
                        action_x = cropHandler.cropParams.w;
                        press_y = y;
                        action_y = cropHandler.cropParams.y;
                    } else if (onArea (CropBottomLeft, x, y)) {
                        state = SResizeBL;
                        press_x = x;
                        action_x = cropHandler.cropParams.x;
                        press_y = y;
                        action_y = cropHandler.cropParams.h;
                    } else if (onArea (CropBottomRight, x, y)) {
                        state = SResizeBR;
                        press_x = x;
                        action_x = cropHandler.cropParams.w;
                        press_y = y;
                        action_y = cropHandler.cropParams.h;
                    } else if (onArea (CropTop, x, y)) {
                        state = SResizeH1;
                        press_y = y;
                        action_y = cropHandler.cropParams.y;
                    } else if (onArea (CropBottom, x, y)) {
                        state = SResizeH2;
                        press_y = y;
                        action_y = cropHandler.cropParams.h;
                    } else if (onArea (CropLeft, x, y)) {
                        state = SResizeW1;
                        press_x = x;
                        action_x = cropHandler.cropParams.x;
                    } else if (onArea (CropRight, x, y)) {
                        state = SResizeW2;
                        press_x = x;
                        action_x = cropHandler.cropParams.w;
                    } else if ((bstate & GDK_SHIFT_MASK) && onArea (CropInside, x, y)) {
                        state = SCropMove;
                        press_x = x;
                        press_y = y;
                        action_x = cropHandler.cropParams.x;
                        action_y = cropHandler.cropParams.y;
                    } else if (onArea (CropObserved, x, y)) {
                        state = SObservedMove;
                        press_x = x;
                        press_y = y;
                        action_x = 0;
                        action_y = 0;
                    } else if (iarea->getToolMode () == TMStraighten) {
                        state = SRotateSelecting;
                        press_x = x;
                        press_y = y;
                        action_x = x;
                        action_y = y;
                        rot_deg = 0;
                    } else if (iarea->getToolMode () == TMSpotWB) {
                        int spotx, spoty;
                        screenCoordToImage (x, y, spotx, spoty);
                        iarea->spotWBSelected (spotx, spoty);
                    } else if (iarea->getToolMode () == TMCropSelect && cropgl) {
                        state = SCropSelecting;
                        screenCoordToImage (x, y, press_x, press_y);
                        cropHandler.cropParams.enabled = true;
                        cropHandler.cropParams.x = press_x;
                        cropHandler.cropParams.y = press_y;
                        cropHandler.cropParams.w = cropHandler.cropParams.h = 1;
                        cropgl->cropInit (cropHandler.cropParams.x, cropHandler.cropParams.y, cropHandler.cropParams.w, cropHandler.cropParams.h);
                    } else if (iarea->getToolMode () == TMHand) {
                        if (editSubscriber) {
                            needRedraw = editSubscriber->button1Pressed(bstate);
                            if ((cropgl && cropgl->inImageArea(iarea->posImage.x, iarea->posImage.y) && (editSubscriber->getEditingType() == ET_PIPETTE && (bstate & GDK_CONTROL_MASK))) || editSubscriber->getEditingType() == ET_OBJECTS) {
                                if (editSubscriber->isDragging()) {
                                    state = SEditDrag1;
                                } else if (editSubscriber->isPicking()) {
                                    state = SEditPick1;
                                    pickedObject = iarea->object;
                                    pickModifierKey = bstate;
                                }
                                press_x = x;
                                press_y = y;
                                action_x = 0;
                                action_y = 0;
                            }
                        }
                        if (state != SEditDrag1) {
                            state = SCropImgMove;
                            press_x = x;
                            press_y = y;
                            action_x = 0;
                            action_y = 0;
                        }
                    } else { // if(zoomSteps[cropZoom].zoom > cropHandler.getFitZoom()) { // only allow move when image is only partial visible
                        state = SCropImgMove;
                        press_x = x;
                        press_y = y;
                        action_x = 0;
                        action_y = 0;
                    }

                } else if (iarea->getToolMode () == TMHand) {  // events outside of the image domain
                    EditSubscriber *editSubscriber = iarea->getCurrSubscriber();

                    if (editSubscriber && editSubscriber->getEditingType() == ET_OBJECTS) {
                        needRedraw = editSubscriber->button1Pressed(bstate);

                        if (editSubscriber->isDragging()) {
                            state = SEditDrag1;
                        } else if (editSubscriber->isPicking()) {
                            state = SEditPick1;
                            pickedObject = iarea->object;
                            pickModifierKey = bstate;
                        }

                        press_x = x;
                        press_y = y;
                        action_x = 0;
                        action_y = 0;
                    }
                }
            }
        }
    } else if (button == 2) {
        if (iarea->getToolMode () == TMHand) {
            EditSubscriber *editSubscriber = iarea->getCurrSubscriber();
            if (editSubscriber && editSubscriber->getEditingType() == ET_OBJECTS) {
                needRedraw = editSubscriber->button2Pressed(bstate);

                if (editSubscriber->isDragging()) {
                    state = SEditDrag2;
                } else if (editSubscriber->isPicking()) {
                    state = SEditPick2;
                    pickedObject = iarea->object;
                    pickModifierKey = bstate;
                }

                press_x = x;
                press_y = y;
                action_x = 0;
                action_y = 0;
            }
        }
    } else if (button == 3) {
        if (iarea->getToolMode () == TMHand) {
            EditSubscriber *editSubscriber = iarea->getCurrSubscriber();
            if (editSubscriber && editSubscriber->getEditingType() == ET_OBJECTS) {
                needRedraw = editSubscriber->button3Pressed(bstate);

                if (editSubscriber->isDragging()) {
                    state = SEditDrag3;
                } else if (editSubscriber->isPicking()) {
                    state = SEditPick3;
                    pickedObject = iarea->object;
                    pickModifierKey = bstate;
                }

                press_x = x;
                press_y = y;
                action_x = 0;
                action_y = 0;
            }
        }
    }

    if (needRedraw) {
        iarea->redraw ();
    }

    updateCursor (x, y);
}

void CropWindow::buttonRelease (int button, int num, int bstate, int x, int y)
{

    EditSubscriber *editSubscriber = iarea->getCurrSubscriber();

    bool needRedraw = false;

    if (state == SCropWinResize) {
        int newWidth = press_x + x - action_x;
        int newHeight = press_y + y - action_y;
        setSize(newWidth, newHeight);

        if (decorated) {
            options.detailWindowWidth = newWidth;
            options.detailWindowHeight = newHeight;
        }

        state = SNormal;

        for (auto listener : listeners) {
            listener->cropWindowSizeChanged (this);
        }

        needRedraw = true;
    } else if (state == SCropImgMove) {
        cropHandler.update ();

        state = SNormal;

        for (std::list<CropWindowListener*>::iterator i = listeners.begin(); i != listeners.end(); i++) {
            (*i)->cropPositionChanged (this);
        }

        needRedraw = true;
    } else if (state == SRotateSelecting) {
        iarea->straightenReady (rot_deg);
        iarea->setToolHand ();
        needRedraw = true;
    } else if (state == SObservedMove) {
        observedCropWin->remoteMoveReady ();
        state = SNormal;
        needRedraw = true;
    } else if (state == SEditDrag1 || state == SEditDrag2 || state == SEditDrag3) {
        if        (state == SEditDrag1) {
            needRedraw = editSubscriber->button1Released();
        } else if (state == SEditDrag2) {
            needRedraw = editSubscriber->button2Released();
        } else if (state == SEditDrag3) {
            needRedraw = editSubscriber->button3Released();
        }

        if (editSubscriber) {
            rtengine::Crop* crop = static_cast<rtengine::Crop*>(cropHandler.getCrop());
            Coord imgPos;
            action_x = x;
            action_y = y;
            screenCoordToImage (x, y, imgPos.x, imgPos.y);

            iarea->posImage.set(imgPos.x, imgPos.y);
            iarea->posScreen.set(x, y);

            Coord cropPos;
            if (state == SEditDrag1 && editSubscriber->getEditingType() == ET_PIPETTE) {
                screenCoordToCropBuffer (x, y, cropPos.x, cropPos.y);

                iarea->object = onArea (CropImage, x, y) && !onArea (CropObserved, x, y) ? 1 : 0;

                //iarea->object = cropgl && cropgl->inImageArea(iarea->posImage.x, iarea->posImage.y) ? 1 : 0;
                if (iarea->object) {
                    crop->getPipetteData(iarea->pipetteVal, cropPos.x, cropPos.y, iarea->getPipetteRectSize());
                    //printf("PipetteData:  %.3f  %.3f  %.3f\n", iarea->pipetteVal[0], iarea->pipetteVal[1], iarea->pipetteVal[2]);
                } else {
                    iarea->pipetteVal[0] = iarea->pipetteVal[1] = iarea->pipetteVal[2] = -1.f;
                }
            } else if (editSubscriber->getEditingType() == ET_OBJECTS) {
                screenCoordToCropCanvas (x, y, cropPos.x, cropPos.y);
                iarea->object = ObjectMOBuffer::getObjectID(cropPos);
            }

            needRedraw |= editSubscriber->mouseOver(bstate);
        } else {
            iarea->object = 0;
        }

        iarea->deltaImage.set(0, 0);
        iarea->deltaScreen.set(0, 0);
        iarea->deltaPrevImage.set(0, 0);
        iarea->deltaPrevScreen.set(0, 0);
    } else if (state == SEditPick1 || state == SEditPick2 || state == SEditPick3) {
        if (editSubscriber) {
            Coord imgPos;
            action_x = x;
            action_y = y;
            screenCoordToImage (x, y, imgPos.x, imgPos.y);

            iarea->posImage.set (imgPos.x, imgPos.y);
            iarea->posScreen.set (x, y);

            Coord cropPos;
            screenCoordToCropCanvas (x, y, cropPos.x, cropPos.y);

            iarea->object = ObjectMOBuffer::getObjectID(cropPos);

            bool elemPicked = iarea->object == pickedObject && bstate == pickModifierKey;

            if        (state == SEditPick1) {
                needRedraw = editSubscriber->pick1 (elemPicked);
            } else if (state == SEditPick2) {
                needRedraw = editSubscriber->pick2 (elemPicked);
            } else if (state == SEditPick3) {
                needRedraw = editSubscriber->pick3 (elemPicked);
            }

            iarea->object = pickedObject = -1;
            pickModifierKey = 0;

            needRedraw |= editSubscriber->mouseOver (bstate);
        } else {
            iarea->object = 0;
        }
    }

    if (cropgl && (state == SCropSelecting || state == SResizeH1 || state == SResizeH2 || state == SResizeW1 || state == SResizeW2 || state == SResizeTL || state == SResizeTR || state == SResizeBL || state == SResizeBR || state == SCropMove)) {
        cropgl->cropManipReady ();
        iarea->setToolHand ();
        needRedraw = true;
    }

    if (decorated) {
        buttonSet.releaseNotify (x, y);
    }

    if (deleted) {
        iarea->flawnOverWindow = NULL;
        delete this;
        return;
    }

    if (state != SEditDrag3 && state != SEditPick3 && button == 3 && !(bstate & (GDK_SHIFT_MASK|GDK_CONTROL_MASK))) {
        iarea->pipetteVal[0] = iarea->pipetteVal[1] = iarea->pipetteVal[2] = -1.f;

        needRedraw = iarea->object == 1;

        if (editSubscriber && editSubscriber->getEditingType() == ET_PIPETTE) {
            editSubscriber->mouseOver(0);
        }

        iarea->setToolHand ();

        if (pmhlistener) {
            pmhlistener->toggleFreeze();
        }
    }

    state = SNormal;
    iarea->grabFocus (NULL);

    if (needRedraw) {
        iarea->redraw ();
    }

    updateCursor (x, y);
}

void CropWindow::pointerMoved (int bstate, int x, int y)
{

    EditSubscriber *editSubscriber = iarea->getCurrSubscriber();

    if (state == SCropWinMove) {
        setPosition (press_x + x - action_x, press_y + y - action_y);
        iarea->redraw ();
    } else if (state == SCropWinResize) {
        setSize (press_x + x - action_x, press_y + y - action_y, true);

        for (auto listener : listeners) {
            listener->cropWindowSizeChanged (this);
        }

        iarea->redraw ();
    } else if (state == SCropImgMove) {
        // multiplier is the amplification factor ; disabled if the user selected "1" (no amplification)
        double factor = options.panAccelFactor == 1 ? 1.0 : options.panAccelFactor * zoomSteps[cropZoom].zoom;

        // never move the preview slower than the cursor
        if (factor < 1.0) {
            factor = 1.0;
        }

        int newAction_x = (press_x - x) / zoomSteps[cropZoom].zoom * factor;
        int newAction_y = (press_y - y) / zoomSteps[cropZoom].zoom * factor;

        int deltaX = newAction_x - action_x;
        int deltaY = newAction_y - action_y;

        action_x =  newAction_x;
        action_y =  newAction_y;

        cropHandler.moveAnchor(deltaX, deltaY, false);

        for (auto listener : listeners) {
            listener->cropPositionChanged (this);
        }

        iarea->redraw ();
    } else if (state == SRotateSelecting) {
        action_x = x;
        action_y = y;
        iarea->redraw ();
    } else if (state == SNormal && iarea->getToolMode () == TMSpotWB) {
        action_x = x;
        action_y = y;
        iarea->redraw ();
    } else if (state == SResizeH1 && cropgl) {
        int oy = cropHandler.cropParams.y;
        cropHandler.cropParams.y = action_y + (y - press_y) / zoomSteps[cropZoom].zoom;
        cropHandler.cropParams.h += oy - cropHandler.cropParams.y;
        cropgl->cropHeight1Resized (cropHandler.cropParams.x, cropHandler.cropParams.y, cropHandler.cropParams.w, cropHandler.cropParams.h);
        iarea->redraw ();
    } else if (state == SResizeH2 && cropgl) {
        cropHandler.cropParams.h = action_y + (y - press_y) / zoomSteps[cropZoom].zoom;
        cropgl->cropHeight2Resized (cropHandler.cropParams.x, cropHandler.cropParams.y, cropHandler.cropParams.w, cropHandler.cropParams.h);
        iarea->redraw ();
    } else if (state == SResizeW1 && cropgl) {
        int ox = cropHandler.cropParams.x;
        cropHandler.cropParams.x = action_x + (x - press_x) / zoomSteps[cropZoom].zoom;
        cropHandler.cropParams.w += ox - cropHandler.cropParams.x;
        cropgl->cropWidth1Resized (cropHandler.cropParams.x, cropHandler.cropParams.y, cropHandler.cropParams.w, cropHandler.cropParams.h);
        iarea->redraw ();
    } else if (state == SResizeW2 && cropgl) {
        cropHandler.cropParams.w = action_x + (x - press_x) / zoomSteps[cropZoom].zoom;
        cropgl->cropWidth2Resized (cropHandler.cropParams.x, cropHandler.cropParams.y, cropHandler.cropParams.w, cropHandler.cropParams.h);
        iarea->redraw ();
    } else if (state == SResizeTL && cropgl) {
        int ox = cropHandler.cropParams.x;
        cropHandler.cropParams.x = action_x + (x - press_x) / zoomSteps[cropZoom].zoom;
        cropHandler.cropParams.w += ox - cropHandler.cropParams.x;
        int oy = cropHandler.cropParams.y;
        cropHandler.cropParams.y = action_y + (y - press_y) / zoomSteps[cropZoom].zoom;
        cropHandler.cropParams.h += oy - cropHandler.cropParams.y;
        cropgl->cropTopLeftResized (cropHandler.cropParams.x, cropHandler.cropParams.y, cropHandler.cropParams.w, cropHandler.cropParams.h);
        iarea->redraw ();
    } else if (state == SResizeTR && cropgl) {
        cropHandler.cropParams.w = action_x + (x - press_x) / zoomSteps[cropZoom].zoom;
        int oy = cropHandler.cropParams.y;
        cropHandler.cropParams.y = action_y + (y - press_y) / zoomSteps[cropZoom].zoom;
        cropHandler.cropParams.h += oy - cropHandler.cropParams.y;
        cropgl->cropTopRightResized (cropHandler.cropParams.x, cropHandler.cropParams.y, cropHandler.cropParams.w, cropHandler.cropParams.h);
        iarea->redraw ();
    } else if (state == SResizeBL && cropgl) {
        int ox = cropHandler.cropParams.x;
        cropHandler.cropParams.x = action_x + (x - press_x) / zoomSteps[cropZoom].zoom;
        cropHandler.cropParams.w += ox - cropHandler.cropParams.x;
        cropHandler.cropParams.h = action_y + (y - press_y) / zoomSteps[cropZoom].zoom;
        cropgl->cropBottomLeftResized (cropHandler.cropParams.x, cropHandler.cropParams.y, cropHandler.cropParams.w, cropHandler.cropParams.h);
        iarea->redraw ();
    } else if (state == SResizeBR && cropgl) {
        cropHandler.cropParams.w = action_x + (x - press_x) / zoomSteps[cropZoom].zoom;
        cropHandler.cropParams.h = action_y + (y - press_y) / zoomSteps[cropZoom].zoom;
        cropgl->cropBottomRightResized (cropHandler.cropParams.x, cropHandler.cropParams.y, cropHandler.cropParams.w, cropHandler.cropParams.h);
        iarea->redraw ();
    } else if (state == SCropMove && cropgl) {
        cropHandler.cropParams.x = action_x + (x - press_x) / zoomSteps[cropZoom].zoom;
        cropHandler.cropParams.y = action_y + (y - press_y) / zoomSteps[cropZoom].zoom;
        cropgl->cropMoved (cropHandler.cropParams.x, cropHandler.cropParams.y, cropHandler.cropParams.w, cropHandler.cropParams.h);
        iarea->redraw ();
    } else if (state == SCropSelecting && cropgl) {
        screenCoordToImage (x, y, action_x, action_y);
        int cx1 = press_x, cy1 = press_y;
        int cx2 = action_x, cy2 = action_y;
        cropgl->cropResized (cx1, cy1, cx2, cy2);

        if (cx2 > cx1) {
            cropHandler.cropParams.x = cx1;
            cropHandler.cropParams.w = cx2 - cx1 + 1;
        } else {
            cropHandler.cropParams.x = cx2;
            cropHandler.cropParams.w = cx1 - cx2 + 1;
        }

        if (cy2 > cy1) {
            cropHandler.cropParams.y = cy1;
            cropHandler.cropParams.h = cy2 - cy1 + 1;
        } else {
            cropHandler.cropParams.y = cy2;
            cropHandler.cropParams.h = cy1 - cy2 + 1;
        }

        iarea->redraw ();
    } else if (state == SObservedMove) {
        int new_action_x = x - press_x;
        int new_action_y = y - press_y;
        observedCropWin->remoteMove ((new_action_x - action_x) / zoomSteps[cropZoom].zoom, (new_action_y - action_y) / zoomSteps[cropZoom].zoom);
        action_x = new_action_x;
        action_y = new_action_y;
        iarea->redraw ();
    } else if (editSubscriber) {
        rtengine::Crop* crop = static_cast<rtengine::Crop*>(cropHandler.getCrop());

        if (state == SNormal || state == SEditPick1 || state == SEditPick2 || state == SEditPick3) {
            Coord imgPos;
            action_x = x;
            action_y = y;
            screenCoordToImage (x, y, imgPos.x, imgPos.y);

            iarea->posImage.set(imgPos.x, imgPos.y);
            iarea->posScreen.set(x, y);

            Coord cropPos;

            if (editSubscriber->getEditingType() == ET_PIPETTE) {
                screenCoordToCropBuffer (x, y, cropPos.x, cropPos.y);

                iarea->object = onArea (CropImage, x, y) && !onArea (CropObserved, x, y) ? 1 : 0;

                //iarea->object = cropgl && cropgl->inImageArea(iarea->posImage.x, iarea->posImage.y) ? 1 : 0;
                if (iarea->object) {
                    crop->getPipetteData(iarea->pipetteVal, cropPos.x, cropPos.y, iarea->getPipetteRectSize());
                    //printf("PipetteData:  %.3f  %.3f  %.3f\n", iarea->pipetteVal[0], iarea->pipetteVal[1], iarea->pipetteVal[2]);
                } else {
                    iarea->pipetteVal[0] = iarea->pipetteVal[1] = iarea->pipetteVal[2] = -1.f;
                }
            } else if (editSubscriber->getEditingType() == ET_OBJECTS) {
                screenCoordToCropCanvas (x, y, cropPos.x, cropPos.y);
                iarea->object = ObjectMOBuffer::getObjectID(cropPos);
            }

            if (editSubscriber->mouseOver(bstate)) {
                iarea->redraw ();
            }
        } else if (state == SEditDrag1 || state == SEditDrag2 || state == SEditDrag3) {
            Coord currPos;
            action_x = x;
            action_y = y;
            Coord oldPosImage = iarea->posImage + iarea->deltaImage;
            //printf(">>> IMG / ImgPrev(%d x %d) = (%d x %d) + (%d x %d)\n", oldPosImage.x, oldPosImage.y, iarea->posImage.x, iarea->posImage.y, iarea->deltaImage.x, iarea->deltaImage.y);
            screenCoordToImage (x, y, currPos.x, currPos.y);
            iarea->deltaImage     = currPos - iarea->posImage;
            iarea->deltaPrevImage = currPos - oldPosImage;
            //printf("          action_ & xy (%d x %d) -> (%d x %d) = (%d x %d) + (%d x %d) / deltaPrev(%d x %d)\n", action_x, action_y, currPos.x, currPos.y, iarea->posImage.x, iarea->posImage.y, iarea->deltaImage.x, iarea->deltaImage.y, iarea->deltaPrevImage.x, iarea->deltaPrevImage.y);

            Coord oldPosScreen = iarea->posScreen + iarea->deltaScreen;
            //printf(">>> SCR / ScrPrev(%d x %d) = (%d x %d) + (%d x %d)\n", oldPosScreen.x, oldPosScreen.y, iarea->posScreen.x, iarea->posScreen.y, iarea->deltaScreen.x, iarea->deltaScreen.y);
            currPos.set(x, y);
            iarea->deltaScreen     = currPos - iarea->posScreen;
            iarea->deltaPrevScreen = currPos - oldPosScreen;
            //printf("          action_ & xy (%d x %d) -> (%d x %d) = (%d x %d) + (%d x %d) / deltaPrev(%d x %d)\n", action_x, action_y, currPos.x, currPos.y, iarea->posScreen.x, iarea->posScreen.y, iarea->deltaScreen.x, iarea->deltaScreen.y, iarea->deltaPrevScreen.x, iarea->deltaPrevScreen.y);

            if (state == SEditDrag1) {
                if (editSubscriber->drag1(bstate)) {
                    iarea->redraw ();
                }
            } else if (state == SEditDrag2) {
                if (editSubscriber->drag2(bstate)) {
                    iarea->redraw ();
                }
            } else if (state == SEditDrag3) {
                if (editSubscriber->drag3(bstate)) {
                    iarea->redraw ();
                }
            }
        }
    }

    updateCursor (x, y);

    bool oRA = onArea (CropResize, x, y);

    if (oRA != onResizeArea) {
        onResizeArea = oRA;
        iarea->redraw ();
    }

    if (decorated) {
        buttonSet.motionNotify (x, y);
    }

    if (pmlistener) {
        int mx, my;
        screenCoordToImage (x, y, mx, my);

        if (!onArea (CropImage, x, y) || !cropHandler.cropPixbuf) {
            cropHandler.getFullImageSize(mx, my);
            //    pmlistener->pointerMoved (false, cropHandler.colorParams.working, mx, my, -1, -1, -1);
            //   if (pmhlistener) pmhlistener->pointerMoved (false, cropHandler.colorParams.working, mx, my, -1, -1, -1);
            /*    Glib::ustring outputProfile;
                outputProfile =cropHandler.colorParams.output ;
                printf("Using \"%s\" output\n", outputProfile.c_str());
                if(outputProfile=="RT_sRGB") printf("OK SRGB2");
            */
            pmlistener->pointerMoved (false, cropHandler.colorParams.output, cropHandler.colorParams.working, mx, my, -1, -1, -1);

            if (pmhlistener) {
                pmhlistener->pointerMoved (false, cropHandler.colorParams.output, cropHandler.colorParams.working, mx, my, -1, -1, -1);
            }

        } else {
            /*MyMutex::MyLock lock(cropHandler.cimg);

            int vx = x - xpos - imgX;
            int vy = y - ypos - imgY;
            guint8* pix = cropHandler.cropPixbuf->get_pixels() + vy*cropHandler.cropPixbuf->get_rowstride() + vx*3;
            if (vx < cropHandler.cropPixbuf->get_width() && vy < cropHandler.cropPixbuf->get_height())
            pmlistener->pointerMoved (true, mx, my, pix[0], pix[1], pix[2]);

            */

            cropHandler.cimg.lock ();
            int vx = x - xpos - imgX;
            int vy = y - ypos - imgY;
//          guint8* pix = cropHandler.cropPixbuf->get_pixels() + vy*cropHandler.cropPixbuf->get_rowstride() + vx*3;
//          if (vx < cropHandler.cropPixbuf->get_width() && vy < cropHandler.cropPixbuf->get_height())
//              pmlistener->pointerMoved (true, mx, my, pix[0], pix[1], pix[2]);
            int imwidth = cropHandler.cropPixbuf->get_width();
            int imheight = cropHandler.cropPixbuf->get_height();
            guint8* pix = cropHandler.cropPixbuftrue->get_pixels() + vy * cropHandler.cropPixbuf->get_rowstride() + vx * 3;

            if (vx < imwidth && vy < imheight) {
                //      pmlistener->pointerMoved (true, cropHandler.colorParams.working, mx, my, pix[0], pix[1], pix[2]);
                pmlistener->pointerMoved (true, cropHandler.colorParams.output, cropHandler.colorParams.working, mx, my, pix[0], pix[1], pix[2]);

                if (pmhlistener)
                    //    pmhlistener->pointerMoved (true, cropHandler.colorParams.working, mx, my, pix[0], pix[1], pix[2]);
                {
                    pmhlistener->pointerMoved (true, cropHandler.colorParams.output, cropHandler.colorParams.working, mx, my, pix[0], pix[1], pix[2]);
                }
            }

            cropHandler.cimg.unlock ();
        }
    }
}

bool CropWindow::onArea (CursorArea a, int x, int y)
{

    int CROPRESIZEBORDER = rtengine::max<int>(9 / zoomSteps[cropZoom].zoom, 3);
    int x1, y1, w, h;

    switch (a) {
    case CropWinButtons:
        return decorated && buttonSet.inside (x, y);

    case CropToolBar:
        return x > xpos && y > ypos && x < xpos + width - 1 && y < ypos + imgAreaY;

    case CropImage:
        return x >= xpos + imgX + imgAreaX && y >= ypos + imgY + imgAreaY && x < xpos + imgX + imgAreaX + imgW && y < ypos + imgY + imgAreaY + imgH;

    case CropBorder:
        return
            (x >= xpos + imgAreaX && y >= ypos + imgAreaY && x < xpos + imgAreaX + imgAreaW && y < ypos + imgAreaY + imgAreaH) &&
            !(x >= xpos + imgX && y >= ypos + imgY && x < xpos + imgX + imgW && y < ypos + imgY + imgH);

    case CropTopLeft:
        screenCoordToImage (x, y, x1, y1);
        return cropHandler.cropParams.enabled &&
               y1 >= cropHandler.cropParams.y - CROPRESIZEBORDER &&
               y1 <= cropHandler.cropParams.y + CROPRESIZEBORDER &&
               y >= ypos + imgY &&
               x1 >= cropHandler.cropParams.x - CROPRESIZEBORDER &&
               x1 <= cropHandler.cropParams.x + CROPRESIZEBORDER &&
               x >= xpos + imgX;

    case CropTopRight:
        screenCoordToImage (x, y, x1, y1);
        return cropHandler.cropParams.enabled &&
               y1 >= cropHandler.cropParams.y - CROPRESIZEBORDER &&
               y1 <= cropHandler.cropParams.y + CROPRESIZEBORDER &&
               y >= ypos + imgY &&
               x1 >= cropHandler.cropParams.x + cropHandler.cropParams.w - 1 - CROPRESIZEBORDER &&
               x1 <= cropHandler.cropParams.x + cropHandler.cropParams.w - 1 + CROPRESIZEBORDER &&
               x < xpos + imgX + imgW;

    case CropBottomLeft:
        screenCoordToImage (x, y, x1, y1);
        return cropHandler.cropParams.enabled &&
               y1 >= cropHandler.cropParams.y + cropHandler.cropParams.h - 1 - CROPRESIZEBORDER &&
               y1 <= cropHandler.cropParams.y + cropHandler.cropParams.h - 1 + CROPRESIZEBORDER &&
               y < ypos + imgY + imgH &&
               x1 >= cropHandler.cropParams.x - CROPRESIZEBORDER &&
               x1 <= cropHandler.cropParams.x + CROPRESIZEBORDER &&
               x >= xpos + imgX;

    case CropBottomRight:
        screenCoordToImage (x, y, x1, y1);
        return cropHandler.cropParams.enabled &&
               y1 >= cropHandler.cropParams.y + cropHandler.cropParams.h - 1 - CROPRESIZEBORDER &&
               y1 <= cropHandler.cropParams.y + cropHandler.cropParams.h - 1 + CROPRESIZEBORDER &&
               y < ypos + imgY + imgH &&
               x1 >= cropHandler.cropParams.x + cropHandler.cropParams.w - 1 - CROPRESIZEBORDER &&
               x1 <= cropHandler.cropParams.x + cropHandler.cropParams.w - 1 + CROPRESIZEBORDER &&
               x < xpos + imgX + imgW;

    case CropTop:
        screenCoordToImage (x, y, x1, y1);
        return cropHandler.cropParams.enabled &&
               x1 > cropHandler.cropParams.x + CROPRESIZEBORDER &&
               x1 < cropHandler.cropParams.x + cropHandler.cropParams.w - 1 - CROPRESIZEBORDER &&
               y1 > cropHandler.cropParams.y - CROPRESIZEBORDER &&
               y1 < cropHandler.cropParams.y + CROPRESIZEBORDER &&
               y >= ypos + imgY;

    case CropBottom:
        screenCoordToImage (x, y, x1, y1);
        return cropHandler.cropParams.enabled &&
               x1 > cropHandler.cropParams.x + CROPRESIZEBORDER &&
               x1 < cropHandler.cropParams.x + cropHandler.cropParams.w - 1 - CROPRESIZEBORDER &&
               y1 > cropHandler.cropParams.y + cropHandler.cropParams.h - 1 - CROPRESIZEBORDER &&
               y1 < cropHandler.cropParams.y + cropHandler.cropParams.h - 1 + CROPRESIZEBORDER &&
               y < ypos + imgY + imgH;

    case CropLeft:
        screenCoordToImage (x, y, x1, y1);
        return cropHandler.cropParams.enabled &&
               y1 > cropHandler.cropParams.y + CROPRESIZEBORDER &&
               y1 < cropHandler.cropParams.y + cropHandler.cropParams.h - 1 - CROPRESIZEBORDER &&
               x1 > cropHandler.cropParams.x - CROPRESIZEBORDER &&
               x1 < cropHandler.cropParams.x + CROPRESIZEBORDER &&
               x >= xpos + imgX;

    case CropRight:
        screenCoordToImage (x, y, x1, y1);
        return cropHandler.cropParams.enabled &&
               y1 > cropHandler.cropParams.y + CROPRESIZEBORDER &&
               y1 < cropHandler.cropParams.y + cropHandler.cropParams.h - 1 - CROPRESIZEBORDER &&
               x1 > cropHandler.cropParams.x + cropHandler.cropParams.w - 1 - CROPRESIZEBORDER &&
               x1 < cropHandler.cropParams.x + cropHandler.cropParams.w - 1 + CROPRESIZEBORDER &&
               x < xpos + imgX + imgW;

    case CropInside:
        screenCoordToImage (x, y, x1, y1);
        return cropHandler.cropParams.enabled &&
               y1 > cropHandler.cropParams.y &&
               y1 < cropHandler.cropParams.y + cropHandler.cropParams.h - 1 &&
               x1 > cropHandler.cropParams.x &&
               x1 < cropHandler.cropParams.x + cropHandler.cropParams.w - 1;

    case CropResize:
        return decorated && x >= xpos + width - 16 && y >= ypos + height - 16 && x < xpos + width && y < ypos + height;

    case CropObserved:
        if (!observedCropWin) {
            return false;
        }

        getObservedFrameArea (x1, y1, w, h);
        return x > x1 - 6 && y > y1 - 6 && x < x1 + w - 1 + 6 && y < y1 + h - 1 + 6 &&
               !(x > x1 + 2 && y > y1 + 2 && x < x1 + w - 1 - 2 && y < y1 + h - 1 - 2);
    }

    return false;
}

void CropWindow::updateCursor (int x, int y)
{

    EditSubscriber *editSubscriber = iarea->getCurrSubscriber();
    ToolMode tm = iarea->getToolMode ();

    if (state == SNormal) {
        if (onArea (CropWinButtons, x, y)) {
            cursorManager.setCursor (iarea->get_window(), CSArrow);
        } else if (onArea (CropToolBar, x, y)) {
            cursorManager.setCursor (iarea->get_window(), CSMove);
        } else if (onArea (CropResize, x, y)) {
            cursorManager.setCursor (iarea->get_window(), CSResizeDiagonal);
        } else if (tm == TMHand && (onArea (CropTopLeft, x, y))) {
            cursorManager.setCursor (iarea->get_window(), CSResizeTopLeft);
        } else if (tm == TMHand && (onArea (CropTopRight, x, y))) {
            cursorManager.setCursor (iarea->get_window(), CSResizeTopRight);
        } else if (tm == TMHand && (onArea (CropBottomLeft, x, y))) {
            cursorManager.setCursor (iarea->get_window(), CSResizeBottomLeft);
        } else if (tm == TMHand && (onArea (CropBottomRight, x, y))) {
            cursorManager.setCursor (iarea->get_window(), CSResizeBottomRight);
        } else if (tm == TMHand && (onArea (CropTop, x, y) || onArea (CropBottom, x, y))) {
            cursorManager.setCursor (iarea->get_window(), CSResizeHeight);
        } else if (tm == TMHand && (onArea (CropLeft, x, y) || onArea (CropRight, x, y))) {
            cursorManager.setCursor (iarea->get_window(), CSResizeWidth);
        } else if (onArea (CropImage, x, y)) {
            int objectID = -1;

            if (editSubscriber && editSubscriber->getEditingType() == ET_OBJECTS) {
                Coord cropPos;
                screenCoordToCropCanvas (iarea->posScreen.x, iarea->posScreen.y, cropPos.x, cropPos.y);
                objectID = ObjectMOBuffer::getObjectID(cropPos);
            }

            if (objectID > -1) {
                cursorManager.setCursor (iarea->get_window(), editSubscriber->getCursor(objectID));
            } else if (tm == TMHand) {
                if (onArea (CropObserved, x, y)) {
                    cursorManager.setCursor (iarea->get_window(), CSMove);
                } else {
                    cursorManager.setCursor (iarea->get_window(), CSOpenHand);
                }
            } else if (tm == TMSpotWB) {
                cursorManager.setCursor (iarea->get_window(), CSSpotWB);
            } else if (tm == TMCropSelect) {
                cursorManager.setCursor (iarea->get_window(), CSCropSelect);
            } else if (tm == TMStraighten) {
                cursorManager.setCursor (iarea->get_window(), CSStraighten);
            }
        } else {
            int objectID = -1;

            if (editSubscriber && editSubscriber->getEditingType() == ET_OBJECTS) {
                Coord cropPos;
                screenCoordToCropCanvas (iarea->posScreen.x, iarea->posScreen.y, cropPos.x, cropPos.y);
                objectID = ObjectMOBuffer::getObjectID(cropPos);
            }

            if (objectID > -1) {
                cursorManager.setCursor (iarea->get_window(), editSubscriber->getCursor(objectID));
            } else {
                cursorManager.setCursor (iarea->get_window(), CSArrow);
            }
        }
    } else if (state == SCropSelecting) {
        cursorManager.setCursor (iarea->get_window(), CSCropSelect);
    } else if (state == SRotateSelecting) {
        cursorManager.setCursor (iarea->get_window(), CSStraighten);
    } else if (state == SCropMove || state == SCropWinMove || state == SObservedMove) {
        cursorManager.setCursor (iarea->get_window(), CSMove);
    } else if (state == SHandMove || state == SCropImgMove) {
        cursorManager.setCursor (iarea->get_window(), CSClosedHand);
    } else if (state == SResizeW1 || state == SResizeW2) {
        cursorManager.setCursor (iarea->get_window(), CSResizeWidth);
    } else if (state == SResizeH1 || state == SResizeH2) {
        cursorManager.setCursor (iarea->get_window(), CSResizeHeight);
    } else if (state == SResizeTL) {
        cursorManager.setCursor (iarea->get_window(), CSResizeTopLeft);
    } else if (state == SResizeTR) {
        cursorManager.setCursor (iarea->get_window(), CSResizeTopRight);
    } else if (state == SResizeBL) {
        cursorManager.setCursor (iarea->get_window(), CSResizeBottomLeft);
    } else if (state == SResizeBR) {
        cursorManager.setCursor (iarea->get_window(), CSResizeBottomRight);
    } else if (state == SCropWinResize) {
        cursorManager.setCursor (iarea->get_window(), CSResizeDiagonal);
    }
}

void CropWindow::expose (Cairo::RefPtr<Cairo::Context> cr)
{
    MyMutex::MyLock lock(cropHandler.cimg);

    if (decorated) {
        drawDecoration (cr);
    }

    int x = xpos, y = ypos;

    // draw the background
    backColor = iarea->previewModePanel->GetbackColor();
    options.bgcolor = backColor;

    if (backColor == 0) {
        Gdk::Color cback = iarea->get_style()->get_bg(Gtk::STATE_NORMAL);
        cr->set_source_rgb (cback.get_red_p(), cback.get_green_p(), cback.get_blue_p());
    } else if (backColor == 1) {
        cr->set_source_rgb (0, 0, 0);
    } else if (backColor == 2) {
        cr->set_source_rgb (1, 1, 1);
    }

    cr->set_line_width (0.);
    cr->rectangle (x + imgAreaX, y + imgAreaY, imgAreaW, imgAreaH);
    cr->stroke_preserve ();
    cr->fill ();


    // draw image
    if (state == SCropImgMove || state == SCropWinResize) {
        // draw a rough image
        int cropX, cropY;
        cropHandler.getPosition (cropX, cropY);

        Glib::RefPtr<Gdk::Pixbuf> rough = iarea->getPreviewHandler()->getRoughImage (cropX, cropY, imgAreaW, imgAreaH, zoomSteps[cropZoom].zoom);

        if (rough) {
            iarea->get_window()->draw_pixbuf (iarea->get_style()->get_base_gc(Gtk::STATE_NORMAL), rough, 0, 0, x + imgAreaX + imgX, y + imgAreaY + imgY, rtengine::min (rough->get_width (), imgAreaW-imgX), rtengine::min (rough->get_height (), imgAreaH-imgY), Gdk::RGB_DITHER_NORMAL, 0, 0);
//            if (cropHandler.cropParams.enabled)
//                drawCrop (cr, x+imgX, y+imgY, imgW, imgH, cropX, cropY, zoomSteps[cropZoom].zoom, cropHandler.cropParams);
        }

        if (observedCropWin) {
            drawObservedFrame (cr);
        }
    } else {
        if (cropHandler.cropPixbuf) {
            imgW = cropHandler.cropPixbuf->get_width ();
            imgH = cropHandler.cropPixbuf->get_height ();
            exposeVersion++;

            bool showcs = iarea->indClippedPanel->showClippedShadows();
            bool showch = iarea->indClippedPanel->showClippedHighlights();
            const bool showR  = iarea->previewModePanel->showR(); // will show clipping if R channel is clipped
            const bool showG  = iarea->previewModePanel->showG(); // will show clipping if G channel is clipped
            const bool showB  = iarea->previewModePanel->showB(); // will show clipping if B channel is clipped
            const bool showL  = iarea->previewModePanel->showL(); // will show clipping if L value   is clipped
            const bool showFocusMask  = iarea->previewModePanel->showFocusMask();

            // While the Right-side ALT is pressed, auto-enable highlight and shadow clipping indicators
            // TODO: Add linux/MacOS specific functions for alternative
#ifdef WIN32

            if (GetKeyState(VK_RMENU) < 0) {
                showcs = true;
                showch = true;
            }

#endif

            if (showcs || showch || showR || showG || showB || showL || showFocusMask) {
                Glib::RefPtr<Gdk::Pixbuf> tmp = cropHandler.cropPixbuf->copy ();
                guint8* pix = tmp->get_pixels();
                guint8* pixWrkSpace = cropHandler.cropPixbuftrue->get_pixels();

                const int pixRowStride = tmp->get_rowstride ();
                const int pixWSRowStride = cropHandler.cropPixbuftrue->get_rowstride ();

                const int bHeight = tmp->get_height();
                const int bWidth = tmp->get_width();

                if (showFocusMask) { // modulate preview to display focus mask
                    const int blur_radius2 = 1;                             // radius of small kernel. 1 => 3x3 kernel
                    const int blur_dim2 = 2 * blur_radius2 + 1;             // dimension of small kernel
                    const int blur_radius = (blur_dim2 * blur_dim2) / 2;    // radius of big kernel
                    const float kernel_size = SQR(2.f * blur_radius + 1.f); // count of pixels in the big blur kernel
                    const float rkernel_size = 1.0f / kernel_size;          // reciprocal of kernel_size to avoid divisions
                    const float kernel_size2 = SQR(2.f * blur_radius2 + 1.f); // count of pixels in the small blur kernel
                    const float rkernel_size2 = 1.0f / kernel_size2;        // reciprocal of kernel_size to avoid divisions

                    // aloocate buffer for precalculated Luminance
                    float* tmpL = (float*)malloc(bHeight * bWidth * sizeof(float) );
                    // aloocate buffers for sums and sums of squares of small kernel
                    float* tmpLsum = (float*)malloc((bHeight) * (bWidth) * sizeof(float) );
                    float* tmpLsumSq = (float*)malloc((bHeight) * (bWidth) * sizeof(float) );
                    float* tmpstdDev2 = (float*)malloc((bHeight) * (bWidth) * sizeof(float) );
                    float maxstdDev_L2 = 0.f;

#ifdef _OPENMP
                    #pragma omp parallel
#endif
                    {
#ifdef _OPENMP
                        #pragma omp for
#endif

                        // precalculate Luminance
                        for(int i = 0; i < bHeight; i++) {
                            guint8* currWS = pixWrkSpace + i * pixWSRowStride;
                            float*  currL = tmpL + i * bWidth;

                            for(int j = 0; j < bWidth; j++) {
                                *currL = 0.299f * (currWS)[0] + 0.587f * (currWS)[1] + 0.114f * (currWS)[2];
                                currL++;
                                currWS += 3;
                            }
                        }

                        float maxthrstdDev_L2 = 0.f;
#ifdef _OPENMP
                        #pragma omp for nowait
#endif

                        // precalculate sum and sum of squares of small kernel
                        for(int i = blur_radius2; i < bHeight - blur_radius2; i++) {
                            for(int j = blur_radius2; j < bWidth - blur_radius2; j++) {
                                float sumL = 0.f;
                                float sumLSqu = 0.f;

                                for(int kh = -blur_radius2; kh <= blur_radius2; kh++) {
                                    for(int kw = -blur_radius2; kw <= blur_radius2; kw++) {
                                        float curL = tmpL[(i + kh) * bWidth + j + kw];
                                        sumL += curL;
                                        sumLSqu += SQR(curL);
                                    }
                                }

                                tmpLsum[i * bWidth + j] = sumL;
                                tmpLsumSq[i * bWidth + j] = sumLSqu;
                                float stdDev_L2 = rkernel_size2 * sqrtf(sumLSqu * kernel_size2 - sumL * sumL);

                                if(stdDev_L2 > maxthrstdDev_L2) {
                                    maxthrstdDev_L2 = stdDev_L2;
                                }

                                tmpstdDev2[i * bWidth + j] = stdDev_L2;
                            }
                        }

                        #pragma omp critical
                        {
                            if(maxthrstdDev_L2 > maxstdDev_L2) {
                                maxstdDev_L2 = maxthrstdDev_L2;
                            }
                        }
                    }

                    const float focus_thresh = 80.f;
                    maxstdDev_L2 = std::min(maxstdDev_L2, focus_thresh);
                    const float focus_threshby10 = focus_thresh / 10.f;
#ifdef _OPENMP
                    #pragma omp parallel for schedule(dynamic,16)
#endif

                    for (int i = blur_radius + 1; i < bHeight - blur_radius; i++) {
                        guint8* curr = pix + i * pixRowStride + 3 * (blur_radius + 1);
                        guint8* currWs = pixWrkSpace + i * pixWSRowStride + 3 * (blur_radius + 1);

                        for (int j = blur_radius + 1; j < bWidth - blur_radius; j++) {

                            //*************
                            // Copyright (c) 2011 Michael Ezra michael@michaelezra.com
                            // determine if pixel is in the sharp area of the image using
                            // standard deviation analysis on two different scales
                            //float focus_thresh2;
                            //float opacity = 0.9;//TODO: implement opacity
                            //TODO: evaluate effects of altering sampling frequency


                            //TODO: dynamically determine appropriate values based on image analysis

                            // calculate average in +-blur_radius pixels area around the current pixel
                            // speed up: calculate sum of squares in the same loops

                            float sum_L = 0.f;
                            float sumsq_L = 0.f;

                            // use precalculated values of small kernel to reduce number of iterations
                            for (int kh = -blur_radius + blur_radius2; kh <= blur_radius - blur_radius2; kh += blur_dim2) {
                                float* currLsum = &tmpLsum[(i + kh) * bWidth + j - blur_radius + 1];
                                float* currLsumSqu = &tmpLsumSq[(i + kh) * bWidth + j - blur_radius + 1];

                                for (int k = -blur_radius + blur_radius2; k <= blur_radius - blur_radius2; k += blur_dim2, currLsum += blur_dim2, currLsumSqu += blur_dim2) {
                                    sum_L += *currLsum;
                                    sumsq_L += *currLsumSqu;
                                }
                            }

                            //float sum_L2 = tmpLsum[i * bWidth + j];
                            //float sumsq_L2 = tmpLsumSq[i * bWidth + j];
                            //*************
                            // averages
                            // Optimized formulas to avoid divisions
                            float stdDev_L = rkernel_size * sqrtf(sumsq_L * kernel_size - sum_L * sum_L);
                            float stdDev_L2 = tmpstdDev2[i * bWidth + j];
//                          float stdDev_L2 = rkernel_size2 * sqrtf(sumsq_L2 * kernel_size2 - sum_L2 * sum_L2);

                            //TODO: try to normalize by average L of the entire (preview) image

                            //detection method 1: detect focus in features
                            //there is no strict condition between stdDev_L and stdDev_L2 themselves
                            /*                                if (stdDev_L2>focus_thresh2
                                                            && (stdDev_L <focus_thresh)){ // this excludes false positives due to high contrast edges

                                                                curr[1]=255;
                                                                curr[0]=0;
                                                                curr[2]=0;

                                                            }*/

                            //detection method 2: detect focus in texture
                            // key point is std deviation on lower scale is higher than for the larger scale
                            // plus some boundary conditions
                            if (focus_thresh >= stdDev_L2 //TODO: could vary this to bypass noise better
                                    && stdDev_L2 > stdDev_L //this is the key to select fine detail within lower contrast on larger scale
                                    && stdDev_L > focus_threshby10 //options.highlightThreshold
                               ) {
                                // transpareny depends on sdtDev_L2 and maxstdDev_L2
                                float transparency = 1.f - std::min(stdDev_L2 / maxstdDev_L2, 1.0f) ;
                                // first row of circle
                                guint8* currtmp = &curr[0] + (-3 * pixRowStride);
                                guint8* currtmpWS = &currWs[0] + (-3 * pixWSRowStride);

                                for(int jj = -3; jj <= 3; jj += 3) {
                                    guint8* currtmpl = currtmp + jj;
                                    guint8* currtmpWSl = currtmpWS + jj;
                                    //transparent green
                                    currtmpl[0] = transparency * currtmpWSl[0];
                                    currtmpl[1] = transparency * currtmpWSl[1] + (1.f - transparency) * 255.f;
                                    currtmpl[2] = transparency * currtmpWSl[2];
                                }

                                // second row of circle
                                currtmp = &curr[0] + (-2 * pixRowStride);
                                currtmpWS = &currWs[0] + (-2 * pixWSRowStride);

                                for(int jj = -6; jj <= 6; jj += 3) {
                                    guint8* currtmpl = currtmp + jj;
                                    guint8* currtmpWSl = currtmpWS + jj;
                                    //transparent green
                                    currtmpl[0] = transparency * currtmpWSl[0];
                                    currtmpl[1] = transparency * currtmpWSl[1] + (1.f - transparency) * 255.f;
                                    currtmpl[2] = transparency * currtmpWSl[2];
                                }

                                // three middle row of circle
                                for(int ii = -1; ii <= 1; ii++) {
                                    currtmp = &curr[0] + (ii * pixRowStride);
                                    currtmpWS = &currWs[0] + (ii * pixWSRowStride);

                                    for(int jj = -9; jj <= 9; jj += 3) {
                                        guint8* currtmpl = currtmp + jj;
                                        guint8* currtmpWSl = currtmpWS + jj;
                                        //transparent green
                                        currtmpl[0] = transparency * currtmpWSl[0];
                                        currtmpl[1] = transparency * currtmpWSl[1] + (1.f - transparency) * 255.f;
                                        currtmpl[2] = transparency * currtmpWSl[2];
                                    }
                                }

                                // second last row of circle
                                currtmp = &curr[0] + (2 * pixRowStride);
                                currtmpWS = &currWs[0] + (2 * pixWSRowStride);

                                for(int jj = -6; jj <= 6; jj += 3) {
                                    guint8* currtmpl = currtmp + jj;
                                    guint8* currtmpWSl = currtmpWS + jj;
                                    //transparent green
                                    currtmpl[0] = transparency * currtmpWSl[0];
                                    currtmpl[1] = transparency * currtmpWSl[1] + (1.f - transparency) * 255.f;
                                    currtmpl[2] = transparency * currtmpWSl[2];
                                }

                                // last row of circle
                                currtmp = &curr[0] + (3 * pixRowStride);
                                currtmpWS = &currWs[0] + (3 * pixWSRowStride);

                                for(int jj = -3; jj <= 3; jj += 3) {
                                    guint8* currtmpl = currtmp + jj;
                                    guint8* currtmpWSl = currtmpWS + jj;
                                    //transparent green
                                    currtmpl[0] = transparency * currtmpWSl[0];
                                    currtmpl[1] = transparency * currtmpWSl[1] + (1.f - transparency) * 255.f;
                                    currtmpl[2] = transparency * currtmpWSl[2];
                                }
                            }

                            curr += 3;
                            currWs += 3;
                        }
                    }

                    free(tmpL);
                    free(tmpLsum);
                    free(tmpLsumSq);
                    free(tmpstdDev2);

                } else { // !showFocusMask

                    const int hlThreshold = options.highlightThreshold;
                    const int shThreshold = options.shadowThreshold;
                    const float ShawdowFac = 64 / (options.shadowThreshold + 1);
                    const float HighlightFac = 64 / (256 - options.highlightThreshold);
                    const bool showclippedAny = (!showR && !showG && !showB && !showL); // will show clipping if any of RGB chanels is clipped

#ifdef _OPENMP
                    #pragma omp parallel for schedule(dynamic,16)
#endif

                    for (int i = 0; i < bHeight; i++) {
                        guint8* curr = pix + i * pixRowStride;
                        guint8* currWS = pixWrkSpace + i * pixWSRowStride;

                        for (int j = 0; j < bWidth; j++) {
                            // we must compare clippings in working space, since the cropPixbuf is in sRGB, with mon profile

                            bool changedHL = false;
                            bool changedSH = false;
                            int delta = 0;
                            // for efficiency, pre-calculate currWS_L as it may be needed in both
                            // if (showch) and if (showcs) branches
                            int currWS_L;

                            if (showL && (showch || showcs)) {
                                currWS_L = (int)(0.299f * currWS[0] + 0.587f * currWS[1] + 0.114f * currWS[2]);
                            }

                            if (showch) {
                                if ((showclippedAny || showR) && currWS[0] >= hlThreshold ) {
                                    delta += 255 - currWS[0];
                                    changedHL = true;
                                }

                                if ((showclippedAny || showG) && currWS[1] >= hlThreshold ) {
                                    delta += 255 - currWS[1];
                                    changedHL = true;
                                }

                                if ((showclippedAny || showB) && currWS[2] >= hlThreshold ) {
                                    delta += 255 - currWS[2];
                                    changedHL = true;
                                }

                                if (showL && currWS_L >= hlThreshold )                     {
                                    delta += 255 - currWS_L ;
                                    changedHL = true;
                                }

                                if (changedHL) {
                                    delta *= HighlightFac;

                                    if (showclippedAny) {
                                        curr[0] = curr[1] = curr[2] = delta;    // indicate clipped highlights in gray
                                    } else {
                                        curr[0] = 255;    // indicate clipped highlights in red
                                        curr[1] = curr[2] = delta;
                                    }
                                }
                            }

                            if (showcs) {
                                if ((showclippedAny || showR) && currWS[0] <= shThreshold ) {
                                    delta += currWS[0];
                                    changedSH = true;
                                }

                                if ((showclippedAny || showG) && currWS[1] <= shThreshold ) {
                                    delta += currWS[1];
                                    changedSH = true;
                                }

                                if ((showclippedAny || showB) && currWS[2] <= shThreshold ) {
                                    delta += currWS[2];
                                    changedSH = true;
                                }

                                if (showL && currWS_L <= shThreshold )                     {
                                    delta += currWS_L ;
                                    changedSH = true;
                                }

                                if (changedSH) {
                                    if (showclippedAny) {
                                        delta = 255 - (delta * ShawdowFac);
                                        curr[0] = curr[1] = curr[2] = delta; // indicate clipped shadows in gray
                                    } else {
                                        delta *= ShawdowFac;
                                        curr[2] = 255;
                                        curr[0] = curr[1] = delta; // indicate clipped shadows in blue
                                    }
                                }
                            } //if (showcs)

                            // modulate the preview of channels & L;
                            if (!changedHL && !changedSH && !showclippedAny) {         //This condition allows clipping indicators for RGB channels to remain in color
                                if (showR) {
                                    curr[1] = curr[2] = curr[0];    //Red   channel in grayscale
                                }

                                if (showG) {
                                    curr[0] = curr[2] = curr[1];    //Green channel in grayscale
                                }

                                if (showB) {
                                    curr[0] = curr[1] = curr[2];    //Blue  channel in grayscale
                                }

                                if (showL) {                        //Luminosity
                                    // see http://en.wikipedia.org/wiki/HSL_and_HSV#Lightness for more info
                                    //int L = (int)(0.212671*curr[0]+0.715160*curr[1]+0.072169*curr[2]);
                                    int L = (int)(0.299 * curr[0] + 0.587 * curr[1] + 0.114 * curr[2]); //Lightness - this matches Luminosity mode in Photoshop CS5
                                    curr[0] = curr[1] = curr[2] = L;
                                }
                            }

                            /*
                                if (showch && (currWS[0]>=options.highlightThreshold || currWS[1]>=options.highlightThreshold || currWS[2]>=options.highlightThreshold))
                                    curr[0] = curr[1] = curr[2] = 0;
                                else if (showcs && (currWS[0]<=options.shadowThreshold || currWS[1]<=options.shadowThreshold || currWS[2]<=options.shadowThreshold))
                                    curr[0] = curr[1] = curr[2] = 255;
                                //if (showch && ((0.299*curr[0]+0.587*curr[1]+0.114*curr[2])>=options.highlightThreshold))
                                //    curr[0] = curr[1] = curr[2] = 0;
                                //else if (showcs && ((0.299*curr[0]+0.587*curr[1]+0.114*curr[2])<=options.shadowThreshold))
                                //    curr[0] = curr[1] = curr[2] = 255;
                            */

                            curr += 3;
                            currWS += 3;
                        }
                    }
                }

                iarea->get_window()->draw_pixbuf (iarea->get_style()->get_base_gc(Gtk::STATE_NORMAL), tmp, 0, 0, x + imgAreaX + imgX, y + imgAreaY + imgY, rtengine::min (tmp->get_width (), imgAreaW-imgX), rtengine::min (tmp->get_height (), imgAreaH-imgY), Gdk::RGB_DITHER_NORMAL, 0, 0);
            } else {
                iarea->get_window()->draw_pixbuf (iarea->get_style()->get_base_gc(Gtk::STATE_NORMAL), cropHandler.cropPixbuf, 0, 0, x + imgAreaX + imgX, y + imgAreaY + imgY, rtengine::min (cropHandler.cropPixbuf->get_width (), imgAreaW-imgX), rtengine::min (cropHandler.cropPixbuf->get_height (), imgAreaH-imgY), Gdk::RGB_DITHER_NORMAL, 0, 0);
            }

            if (cropHandler.cropParams.enabled) {
                int cropX, cropY;
                cropHandler.getPosition (cropX, cropY);
                drawCrop (cr, x + imgAreaX + imgX, y + imgAreaY + imgY, imgW, imgH, cropX, cropY, zoomSteps[cropZoom].zoom, cropHandler.cropParams, (this == iarea->mainCropWindow), true, zoomSteps[cropZoom].zoom <= cropHandler.getFitZoom() );
            }

            if (observedCropWin) {
                drawObservedFrame (cr);
            }

            EditSubscriber *editSubscriber = iarea->getCurrSubscriber();
            if (editSubscriber && editSubscriber->getEditingType() == ET_OBJECTS && bufferCreated()) {

                if (this != iarea->mainCropWindow) {
                    cr->set_line_width (0.);
                    cr->rectangle (x + imgAreaX, y + imgAreaY, imgAreaW, imgAreaH);
                    cr->clip();
                }

                // drawing Subscriber's visible geometry
                const std::vector<Geometry*> visibleGeom = editSubscriber->getVisibleGeometry();
                cr->set_antialias(Cairo::ANTIALIAS_DEFAULT); // ANTIALIAS_SUBPIXEL ?
                cr->set_line_cap(Cairo::LINE_CAP_SQUARE);
                cr->set_line_join(Cairo::LINE_JOIN_ROUND);

                // drawing outer lines
                for (auto geom : visibleGeom) {
                    geom->drawOuterGeometry(cr, this, *this);
                }

                // drawing inner lines
                for (auto geom : visibleGeom) {
                    geom->drawInnerGeometry(cr, this, *this);
                }

                // drawing to the "mouse over" channel
                const auto mouseOverGeom = editSubscriber->getMouseOverGeometry();
                if (mouseOverGeom.size()) {
                    if (mouseOverGeom.size() > 65534) {
                        // once it has been switched to OM_65535, it won't return back to OM_255
                        // to avoid constant memory allocations in some particular situation.
                        // It will return to OM_255 on a new editing session
                        setObjectMode(OM_65535);
                    }

                    Cairo::RefPtr<Cairo::Context> crMO = Cairo::Context::create(ObjectMOBuffer::getObjectMap());
                    crMO->set_antialias(Cairo::ANTIALIAS_NONE);
                    crMO->set_line_cap(Cairo::LINE_CAP_SQUARE);
                    crMO->set_line_join(Cairo::LINE_JOIN_ROUND);
                    crMO->set_operator(Cairo::OPERATOR_SOURCE);

                    // clear the bitmap
                    crMO->set_source_rgba(0., 0., 0., 0.);
                    crMO->rectangle(0., 0., ObjectMOBuffer::getObjectMap()->get_width(), ObjectMOBuffer::getObjectMap()->get_height());
                    crMO->set_line_width(0.);
                    crMO->fill();

                    int a=0;
                    for (auto moGeom : mouseOverGeom) {
                        moGeom->drawToMOChannel(crMO, a, this, *this);
                        ++a;
                    }
                }
                if (this != iarea->mainCropWindow) {
                    cr->reset_clip();
                }

            }
        } else {
            // cropHandler.cropPixbuf is null
            int cropX, cropY;
            cropHandler.getPosition (cropX, cropY);
            Glib::RefPtr<Gdk::Pixbuf> rough = iarea->getPreviewHandler()->getRoughImage (cropX, cropY, imgAreaW, imgAreaH, zoomSteps[cropZoom].zoom);

            if (rough) {
                iarea->get_window()->draw_pixbuf (iarea->get_style()->get_base_gc(Gtk::STATE_NORMAL), rough, 0, 0, x + imgAreaX + imgX, y + imgAreaY + imgY, rtengine::min (rough->get_width (), imgAreaW-imgX), rtengine::min (rough->get_height (), imgAreaH-imgY), Gdk::RGB_DITHER_NORMAL, 0, 0);

                if (cropHandler.cropParams.enabled) {
                    drawCrop (cr, x + imgAreaX + imgX, y + imgAreaY + imgY, rough->get_width(), rough->get_height(), cropX, cropY, zoomSteps[cropZoom].zoom, cropHandler.cropParams, (this == iarea->mainCropWindow), true, zoomSteps[cropZoom].zoom <= cropHandler.getFitZoom());
                }

                if (observedCropWin) {
                    drawObservedFrame (cr);
                }
            }
        }
    }

    if (state == SRotateSelecting) {
        drawStraightenGuide (cr);
    }

    if (state == SNormal && isFlawnOver) {
        EditSubscriber *editSubscriber = iarea->getCurrSubscriber();

        if (iarea->getToolMode () == TMHand && editSubscriber && editSubscriber->getEditingType() == ET_PIPETTE && iarea->object) {
            drawUnscaledSpotRectangle (cr, iarea->getPipetteRectSize ());
        } else if (iarea->getToolMode () == TMSpotWB) {
            drawScaledSpotRectangle (cr, iarea->getSpotWBRectSize ());
        }
    }

    //t2.set ();
//    printf ("etime --> %d, %d\n", t2.etime (t1), t4.etime (t3));
}

void CropWindow::setEditSubscriber (EditSubscriber* newSubscriber) {
    // Delete, create, update all buffers based upon newSubscriber's type
    if (newSubscriber) {
        ObjectMOBuffer::resize (imgAreaW, imgAreaH);
    } else {
        ObjectMOBuffer::flush ();
    }
    cropHandler.setEditSubscriber(newSubscriber);
}

// zoom* is called from the zoomPanel or the scroll wheel in the preview area
void CropWindow::zoomIn (bool toCursor, int cursorX, int cursorY)
{

    int x = -1;
    int y = -1;

    if (toCursor) {
        x = cursorX;
        y = cursorY;
    } else {
        if (zoomSteps[cropZoom].zoom <= cropHandler.getFitZoom()) {
            if (cropHandler.cropParams.enabled) {
                x = cropHandler.cropParams.x + cropHandler.cropParams.w / 2;
                y = cropHandler.cropParams.y + cropHandler.cropParams.h / 2;
            } else {
                int fw, fh;
                cropHandler.getFullImageSize(fw, fh);
                x = fw / 2;
                y = fh / 2;
            }

            zoomVersion = exposeVersion;
        } else if (zoomVersion != exposeVersion) {
            screenCoordToImage(xpos + imgX + imgW / 2, ypos + imgY + imgH / 2, x, y);

            if (cropHandler.cropParams.enabled) {
                // add some gravity towards crop center
                int x1 = cropHandler.cropParams.x + cropHandler.cropParams.w / 2;
                int y1 = cropHandler.cropParams.y + cropHandler.cropParams.h / 2;
                double cropd = sqrt(cropHandler.cropParams.h * cropHandler.cropParams.h + cropHandler.cropParams.w * cropHandler.cropParams.w) * zoomSteps[cropZoom].zoom;
                double imd = sqrt(imgW * imgW + imgH + imgH);
                double d;

                // the more we can see of the crop, the more gravity towards crop center
                if (cropd > imd) {
                    d = 0.8;
                } else if (cropd < imd * 0.5) {
                    d = 0.0;
                } else {
                    d = 1.6 * (cropd - imd * 0.5) / imd;
                }

                x = d * x + (1.0 - d) * x1;
                y = d * y + (1.0 - d) * y1;
            }

            zoomVersion = exposeVersion;
        }
    }

    changeZoom (cropZoom + 1, true, x, y);
    fitZoom = false;
}

void CropWindow::zoomOut (bool toCursor, int cursorX, int cursorY)
{

    int x = -1;
    int y = -1;

    if (toCursor) {
        x = cursorX;
        y = cursorY;
    }

    zoomVersion = exposeVersion;
    changeZoom (cropZoom - 1, true, x, y);
    fitZoom = false;
}

void CropWindow::zoom11 ()
{

    int x = -1;
    int y = -1;

    if (zoomSteps[cropZoom].zoom <= cropHandler.getFitZoom()) {
        if (cropHandler.cropParams.enabled) {
            x = cropHandler.cropParams.x + cropHandler.cropParams.w / 2;
            y = cropHandler.cropParams.y + cropHandler.cropParams.h / 2;
        } else {
            int fw, fh;
            cropHandler.getFullImageSize(fw, fh);
            x = fw / 2;
            y = fh / 2;
        }

        zoomVersion = exposeVersion;
    }

    changeZoom (ZOOM11INDEX, true, x, y);
    fitZoom = false;
}

double CropWindow::getZoom ()
{

    return zoomSteps[cropZoom].zoom;
}

bool CropWindow::isMinZoom ()
{
    return cropZoom <= 0;
}

bool CropWindow::isMaxZoom ()
{
    return cropZoom >= MAXZOOMSTEPS;
}

void CropWindow::setZoom (double zoom)
{
    int cz = MAXZOOMSTEPS;

    if (zoom < zoomSteps[0].zoom) {
        cz = 0;
    } else
        for (int i = 0; i < MAXZOOMSTEPS; i++)
            if (zoomSteps[i].zoom <= zoom && zoomSteps[i + 1].zoom > zoom) {
                cz = i;
                break;
            }

    changeZoom (cz, false);
}

double CropWindow::getZoomFitVal ()
{
    double z = cropHandler.getFitZoom ();
    int cz = MAXZOOMSTEPS;

    if (z < zoomSteps[0].zoom) {
        cz = 0;
    } else
        for (int i = 0; i < MAXZOOMSTEPS; i++)
            if (zoomSteps[i].zoom <= z && zoomSteps[i + 1].zoom > z) {
                cz = i;
                break;
            }

    return zoomSteps[cz].zoom;
}


void CropWindow::zoomFit ()
{

    double z = cropHandler.getFitZoom ();
    int cz = MAXZOOMSTEPS;

    if (z < zoomSteps[0].zoom) {
        cz = 0;
    } else
        for (int i = 0; i < MAXZOOMSTEPS; i++)
            if (zoomSteps[i].zoom <= z && zoomSteps[i + 1].zoom > z) {
                cz = i;
                break;
            }

    zoomVersion = exposeVersion;
    changeZoom (cz, true, -1, -1);
    fitZoom = true;
}

void CropWindow::zoomFitCrop ()
{
    if(cropHandler.cropParams.enabled) {
        double z = cropHandler.getFitCropZoom ();
        int cz = MAXZOOMSTEPS;

        if (z < zoomSteps[0].zoom) {
            cz = 0;
        } else
            for (int i = 0; i < MAXZOOMSTEPS; i++)
                if (zoomSteps[i].zoom <= z && zoomSteps[i + 1].zoom > z) {
                    cz = i;
                    break;
                }

        zoomVersion = exposeVersion;
        int centerX, centerY;
        centerX = cropHandler.cropParams.x + cropHandler.cropParams.w / 2;
        centerY = cropHandler.cropParams.y + cropHandler.cropParams.h / 2;
        changeZoom (cz, true, centerX, centerY);
        fitZoom = false;
    }
}

void CropWindow::buttonPressed (LWButton* button, int actionCode, void* actionData)
{

    if (button == bZoomIn) { // zoom in
        zoomIn ();
    } else if (button == bZoomOut) { // zoom out
        zoomOut ();
    } else if (button == bZoom100) { // zoom 100
        zoom11 ();
    } else if (button == bClose) { // close
        if(ipc->updateTryLock()) {
            deleted = true;
            iarea->cropWindowClosed (this);
            ipc->updateUnLock();
        }
    }
}

void CropWindow::redrawNeeded (LWButton* button)
{

    iarea->redraw ();
}

void CropWindow::changeZoom  (int zoom, bool notify, int centerx, int centery)
{

    if (zoom < 0) {
        zoom = 0;
    } else if (zoom > MAXZOOMSTEPS) {
        zoom = MAXZOOMSTEPS;
    }

    cropZoom = zoom;

    cropLabel = zoomSteps[cropZoom].label;
    cropHandler.setZoom (zoomSteps[cropZoom].czoom, centerx, centery);

    if (notify)
        for (auto listener : listeners) {
            listener->cropZoomChanged (this);
        }

    iarea->redraw ();
}

void CropWindow::screenCoordToCropBuffer (int phyx, int phyy, int& cropx, int& cropy)
{

    rtengine::Crop* crop = static_cast<rtengine::Crop*>(cropHandler.getCrop());
    cropx = phyx - xpos - imgX - imgAreaX;
    cropy = phyy - ypos - imgY - imgAreaY;

    if (zoomSteps[cropZoom].zoom > 1.) {
        cropx = int(double(cropx) / zoomSteps[cropZoom].zoom);
        cropy = int(double(cropy) / zoomSteps[cropZoom].zoom);
    }

    cropx += crop->getLeftBorder();
    cropy += crop->getUpperBorder();
}

void CropWindow::screenCoordToImage (int phyx, int phyy, int& imgx, int& imgy)
{

    int cropX, cropY;
    cropHandler.getPosition (cropX, cropY);
    imgx = cropX + (phyx - xpos - imgX) / zoomSteps[cropZoom].zoom;
    imgy = cropY + (phyy - ypos - imgY) / zoomSteps[cropZoom].zoom;
}

void CropWindow::screenCoordToCropCanvas (int phyx, int phyy, int& prevx, int& prevy)
{

    prevx = phyx - xpos - imgAreaX;
    prevy = phyy - ypos - imgAreaY;
}

void CropWindow::imageCoordToScreen (int imgx, int imgy, int& phyx, int& phyy)
{

    int cropX, cropY;
    cropHandler.getPosition (cropX, cropY);
    phyx = (imgx - cropX) * zoomSteps[cropZoom].zoom + xpos + imgX + imgAreaX;
    phyy = (imgy - cropY) * zoomSteps[cropZoom].zoom + ypos + imgY + imgAreaY;
}

void CropWindow::imageCoordToCropCanvas (int imgx, int imgy, int& phyx, int& phyy)
{

    int cropX, cropY;
    cropHandler.getPosition (cropX, cropY);
    phyx = (imgx - cropX) * zoomSteps[cropZoom].zoom + imgX;
    phyy = (imgy - cropY) * zoomSteps[cropZoom].zoom + imgY;
}

void CropWindow::imageCoordToCropBuffer (int imgx, int imgy, int& phyx, int& phyy)
{
    int cropX, cropY;
    rtengine::Crop* crop = static_cast<rtengine::Crop*>(cropHandler.getCrop());
    cropHandler.getPosition (cropX, cropY);
    phyx = (imgx - cropX) * zoomSteps[cropZoom].zoom + /*xpos + imgX +*/ crop->getLeftBorder();
    phyy = (imgy - cropY) * zoomSteps[cropZoom].zoom + /*ypos + imgY +*/ crop->getUpperBorder();
}

int CropWindow::scaleValueToImage (int value)
{
    return int(double(value) / zoomSteps[cropZoom].zoom);
}

float CropWindow::scaleValueToImage (float value)
{
    return float(double(value) / zoomSteps[cropZoom].zoom);
}

double CropWindow::scaleValueToImage (double value)
{
    return value / zoomSteps[cropZoom].zoom;
}

int CropWindow::scaleValueToCanvas (int value)
{
    return int(double(value) * zoomSteps[cropZoom].zoom);
}

float CropWindow::scaleValueToCanvas (float value)
{
    return float(double(value) * zoomSteps[cropZoom].zoom);
}

double CropWindow::scaleValueToCanvas (double value)
{
    return value * zoomSteps[cropZoom].zoom;
}

void CropWindow::drawDecoration (Cairo::RefPtr<Cairo::Context> cr)
{

    int x = xpos, y = ypos;
    // prepare label
    Glib::RefPtr<Pango::Context> context = iarea->get_pango_context () ;
    Pango::FontDescription fontd = context->get_font_description ();
    fontd.set_weight (Pango::WEIGHT_BOLD);
    fontd.set_size(8 * Pango::SCALE);
    context->set_font_description (fontd);
    Glib::RefPtr<Pango::Layout> cllayout = iarea->create_pango_layout(cropLabel);
    int iw, ih;
    cllayout->get_pixel_size (iw, ih);

    // draw decoration (border)
    int h = height, w = width;

    cr->set_source_rgb (0.1, 0.1, 0.1);
    cr->set_line_width (1.0);
    cr->move_to (x + 2.5, y + titleHeight + 2.5 );
    cr->line_to (x + 2.5, y + h - 2.5);
    cr->line_to (x + w - 2.5, y + h - 2.5);
    cr->line_to (x + w - 2.5, y + titleHeight + 2.5 );

    cr->set_source_rgba (0.0, 0.0, 0.0, 0.5);
    cr->rectangle (x + 2.5, y + 0.5, w - 5, titleHeight + 2);
    cr->stroke_preserve ();
    cr->fill ();

    // draw label
    cr->set_source_rgba (1, 1, 1, 0.5);
    cr->move_to (x + 10 + sideBorderWidth + bZoomIn->getIcon()->get_width() + bZoomOut->getIcon()->get_width() + bZoom100->getIcon()->get_width(), y + 1 + upperBorderWidth + (titleHeight - ih) / 2);
    cllayout->add_to_cairo_context (cr);
    cr->fill ();

    buttonSet.redraw (cr);
}

void CropWindow::drawStraightenGuide (Cairo::RefPtr<Cairo::Context> cr)
{

    if (action_x != press_x || action_y != press_y) {
        double arg = (press_x - action_x) / sqrt(double((press_x - action_x) * (press_x - action_x) + (press_y - action_y) * (press_y - action_y)));
        double sol1, sol2;
        double pi = M_PI;

        if (press_y > action_y) {
            sol1 = acos(arg) * 180 / pi;
            sol2 = -acos(-arg) * 180 / pi;
        } else {
            sol1 = acos(-arg) * 180 / pi;
            sol2 = -acos(arg) * 180 / pi;
        }

        if (fabs(sol1) < fabs(sol2)) {
            rot_deg = sol1;
        } else {
            rot_deg = sol2;
        }

        if (rot_deg < -45) {
            rot_deg = 90.0 + rot_deg;
        } else if (rot_deg > 45) {
            rot_deg = - 90.0 + rot_deg;
        }
    } else {
        rot_deg = 0;
    }

    Glib::RefPtr<Pango::Context> context = iarea->get_pango_context () ;
    Pango::FontDescription fontd = context->get_font_description ();
    fontd.set_weight (Pango::WEIGHT_BOLD);
    fontd.set_size (8 * Pango::SCALE);
    context->set_font_description (fontd);
    Glib::RefPtr<Pango::Layout> deglayout = iarea->create_pango_layout(Glib::ustring::compose ("%1 deg", Glib::ustring::format(std::setprecision(2), rot_deg)));

    int x1 = press_x;
    int y1 = press_y;
    int y2 = action_y;
    int x2 = action_x;
    /*    if (x1<0) x1 = 0;
        if (y1<0) y1 = 0;
        if (x2<0) x2 = 0;
        if (y2<0) y2 = 0;
        if (x2>=image->getWidth()) x2 = image->getWidth()-1;
        if (y2>=image->getHeight()) y2 = image->getHeight()-1;
        if (x1>=image->getWidth()) x1 = image->getWidth()-1;
        if (y1>=image->getHeight()) y1 = image->getHeight()-1;
    */

    cr->set_line_width (1);
    cr->set_source_rgba (1.0, 1.0, 1.0, 0.618);
    cr->move_to (x1 + 0.5, y1 + 0.5);
    cr->line_to (x2 + 0.5, y2 + 0.5);
    cr->stroke ();
    cr->set_source_rgba (0.0, 0.0, 0.0, 0.618);
    std::valarray<double> ds (1);
    ds[0] = 4;
    cr->set_dash (ds, 0);
    cr->move_to (x1 + 0.5, y1 + 0.5);
    cr->line_to (x2 + 0.5, y2 + 0.5);
    cr->stroke ();

    if (press_x != action_x && press_y != action_y) {
        cr->set_source_rgb (0.0, 0.0, 0.0);
        cr->move_to ((x1 + x2) / 2 + 1, (y1 + y2) / 2 + 1);
        deglayout->add_to_cairo_context (cr);
        cr->move_to ((x1 + x2) / 2 + 1, (y1 + y2) / 2 - 1);
        deglayout->add_to_cairo_context (cr);
        cr->move_to ((x1 + x2) / 2 - 1, (y1 + y2) / 2 + 1);
        deglayout->add_to_cairo_context (cr);
        cr->move_to ((x1 + x2) / 2 + 1, (y1 + y2) / 2 + 1);
        deglayout->add_to_cairo_context (cr);
        cr->fill ();
        cr->set_source_rgb (1.0, 1.0, 1.0);
        cr->move_to ((x1 + x2) / 2, (y1 + y2) / 2);
        deglayout->add_to_cairo_context (cr);
        cr->fill ();
    }
}

void CropWindow::drawScaledSpotRectangle (Cairo::RefPtr<Cairo::Context> cr, int rectSize)
{

    int x1 = action_x / zoomSteps[cropZoom].zoom - rectSize;
    int y1 = action_y / zoomSteps[cropZoom].zoom - rectSize;
    int y2 = action_y / zoomSteps[cropZoom].zoom + rectSize;
    int x2 = action_x / zoomSteps[cropZoom].zoom + rectSize;

    cr->set_line_width (1.0);
    cr->rectangle (xpos + imgX + imgAreaX - 0.5, ypos + imgY + imgAreaY - 0.5, imgAreaW, imgAreaH);
    cr->clip ();

    cr->set_source_rgb (1.0, 1.0, 1.0);
    cr->rectangle (x1 * zoomSteps[cropZoom].zoom - 1.5, y1 * zoomSteps[cropZoom].zoom - 1.5, x2 * zoomSteps[cropZoom].zoom - x1 * zoomSteps[cropZoom].zoom + 2, y2 * zoomSteps[cropZoom].zoom - y1 * zoomSteps[cropZoom].zoom + 2);
    cr->stroke ();
    cr->set_source_rgb (0.0, 0.0, 0.0);
    cr->rectangle (x1 * zoomSteps[cropZoom].zoom - 0.5, y1 * zoomSteps[cropZoom].zoom - 0.5, x2 * zoomSteps[cropZoom].zoom - x1 * zoomSteps[cropZoom].zoom, y2 * zoomSteps[cropZoom].zoom - y1 * zoomSteps[cropZoom].zoom);
    cr->stroke ();

    cr->reset_clip ();
}

void CropWindow::drawUnscaledSpotRectangle (Cairo::RefPtr<Cairo::Context> cr, int rectSize)
{

    int x1 = action_x - rectSize;
    int y1 = action_y - rectSize;
    int y2 = action_y + rectSize;
    int x2 = action_x + rectSize;

    cr->set_line_width (1.0);
    cr->rectangle (xpos + imgX + imgAreaX - 0.5, ypos + imgY + imgAreaY - 0.5, imgAreaW, imgAreaH);
    cr->clip ();

    cr->set_source_rgb (1.0, 1.0, 1.0);
    cr->rectangle (x1 - 1.5, y1 - 1.5, x2 - x1 + 2, y2 - y1 + 2);
    cr->stroke ();
    cr->set_source_rgb (0.0, 0.0, 0.0);
    cr->rectangle (x1 - 0.5, y1 - 0.5, x2 - x1, y2 - y1);
    cr->stroke ();

    cr->reset_clip ();
}

void CropWindow::getObservedFrameArea (int& x, int& y, int& w, int& h, int rw, int rh)
{

    int observedCropX, observedCropY, observedCropW, observedCropH;
    observedCropWin->getCropRectangle (observedCropX, observedCropY, observedCropW, observedCropH);
    int mainCropX, mainCropY, mainCropW, mainCropH;
    getCropRectangle (mainCropX, mainCropY, mainCropW, mainCropH);

    // translate it to screen coordinates
    if (rw) {  // rw and rh are the rough image's dimension
        x = xpos + imgAreaX + (imgAreaW - rw) / 2 + (observedCropX - mainCropX) * zoomSteps[cropZoom].zoom;
        y = ypos + imgAreaY + (imgAreaH - rh) / 2 + (observedCropY - mainCropY) * zoomSteps[cropZoom].zoom;
    } else {
        x = xpos + imgX + (observedCropX - mainCropX) * zoomSteps[cropZoom].zoom;
        y = ypos + imgY + (observedCropY - mainCropY) * zoomSteps[cropZoom].zoom;
    }

    w = observedCropW * zoomSteps[cropZoom].zoom;
    h = observedCropH * zoomSteps[cropZoom].zoom;
}

void CropWindow::drawObservedFrame (Cairo::RefPtr<Cairo::Context> cr, int rw, int rh)
{

    int x, y, w, h;
    getObservedFrameArea (x, y, w, h, rw, rh);

    // draw a black "shadow" line
    cr->set_source_rgba( 0, 0, 0, 0.65);
    cr->set_line_width (1);
    cr->rectangle (x - 0.5, y - 0.5, w + 4, h + 4);
    cr->stroke ();

    // draw a "frame" line. Color of frame line can be set in preferences
    cr->set_source_rgba(options.navGuideBrush[0], options.navGuideBrush[1], options.navGuideBrush[2], options.navGuideBrush[3]); //( 1, 1, 1, 1.0);
    cr->rectangle (x - 1.5, y - 1.5, w + 4, h + 4);
    cr->stroke ();
}

void CropWindow::cropImageUpdated ()
{

    iarea->redraw ();
}

void CropWindow::cropWindowChanged ()
{

    if (!decorated) {
        iarea->syncBeforeAfterViews ();
    }

    iarea->redraw ();
}

void CropWindow::initialImageArrived ()
{

    for (auto listener : listeners) {
        listener->initialImageArrived (this);
    }
}

void CropWindow::setDisplayPosition (int x, int y) {
    imgX = x;
    imgY = y;
}

void CropWindow::remoteMove (int deltaX, int deltaY)
{

    state = SCropImgMove;
    cropHandler.moveAnchor(deltaX, deltaY, false);

    for (auto listener : listeners) {
        listener->cropPositionChanged (this);
    }
}

void CropWindow::remoteMoveReady ()
{

    cropHandler.update ();
    state = SNormal;

    for (auto listener : listeners) {
        listener->cropPositionChanged (this);
    }
}

void CropWindow::delCropWindowListener (CropWindowListener* l)
{

    std::list<CropWindowListener*>::iterator i = listeners.begin();

    while (i != listeners.end())
        if (*i == l) {
            i = listeners.erase (i);
        } else {
            i++;
        }
}

EditDataProvider* CropWindow::getImageArea()
{
    return iarea;
}
