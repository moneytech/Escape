/**
 * $Id$
 * Copyright (C) 2008 - 2011 Nils Asmussen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#pragma once

#include <esc/common.h>
#include <esc/messages.h>
#include <gui/graphics/graphics.h>
#include <gui/event/event.h>
#include <gui/event/subscriber.h>
#include <gui/application.h>
#include <gui/theme.h>
#include <vector>

namespace gui {
	class Control;
	class ScrollPane;

	/**
	 * The abstract base class for all UI-elements (windows, controls). It has a position, a size,
	 * a graphics object and has callback-methods for events.
	 */
	class UIElement {
		friend class Window;
		friend class Panel;
		friend class Control;
		friend class ScrollPane;

	public:
		typedef unsigned id_type;
		typedef Sender<UIElement&,const MouseEvent&> mouseev_type;
		typedef Sender<UIElement&,const KeyEvent&> keyev_type;

		/**
		 * Creates an ui-element at position 0,0 and 0x0 pixels large. When using a layout, this
		 * will determine the actual position and size.
		 */
		UIElement()
			: _id(_nextid++), _g(NULL), _parent(NULL), _theme(Application::getInstance()->getDefaultTheme()),
			  _x(0), _y(0), _width(0), _height(0), _mouseMoved(), _mousePressed(), _mouseReleased(),
			  _mouseWheel(), _keyPressed(), _keyReleased(), _enableRepaint(true),
			  _prefWidth(0), _prefHeight(0) {
		};
		/**
		 * Constructor that specifies a position and size explicitly. This can be used if no layout
		 * is used or if a different preferred size than the min size is desired.
		 *
		 * @param x the x-position
		 * @param y the y-position
		 * @param width the width
		 * @param height the height
		 */
		UIElement(gpos_t x,gpos_t y,gsize_t width,gsize_t height)
			: _id(_nextid++), _g(NULL), _parent(NULL), _theme(Application::getInstance()->getDefaultTheme()),
			  _x(x), _y(y), _width(width), _height(height), _mouseMoved(), _mousePressed(), _mouseReleased(),
			  _mouseWheel(), _keyPressed(), _keyReleased(), _enableRepaint(true), _prefWidth(width),
			  _prefHeight(height) {
		};
		/**
		 * Destructor. Free's the memory
		 */
		virtual ~UIElement() {
			delete _g;
		};

		/**
		 * @return the id of this UIElement (unique in one application)
		 */
		inline id_type id() const {
			return _id;
		};

		/**
		 * @return the x-position of this element relative to the parent
		 */
		inline gpos_t getX() const {
			return _x;
		};
		/**
		 * @return the x-position in the window
		 */
		gpos_t getWindowX() const;
		/**
		 * @return the x-position on the screen
		 */
		gpos_t getScreenX() const;
		/**
		 * @return the y-position of this element relative to the parent
		 */
		inline gpos_t getY() const {
			return _y;
		};
		/**
		 * @return the y-position in the window
		 */
		gpos_t getWindowY() const;
		/**
		 * @return the y-position on the screen
		 */
		gpos_t getScreenY() const;
		/**
		 * @return the width of this element
		 */
		inline gsize_t getWidth() const {
			return _width;
		};
		/**
		 * @return the height of this element
		 */
		inline gsize_t getHeight() const {
			return _height;
		};

		/**
		 * @return the width of the content of this ui-element. for normal elements its simply
		 * 	their width. containers may reduce it if they want to paint other stuff besides
		 * 	their children.
		 */
		virtual gsize_t getContentWidth() const {
			// we have to use the minimum of our size and the parent's because some containers
			// above us might restrict the size, while some won't.
			if(_parent)
				return min(_parent->getContentWidth(),_width);
			return _width;
		};
		/**
		 * @return the height of the content of this ui-element. for normal elements its simply
		 * 	their height. containers may reduce it if they want to paint other stuff besides
		 * 	their children.
		 */
		virtual gsize_t getContentHeight() const {
			if(_parent)
				return min(_parent->getContentHeight(),_height);
			return _height;
		};

		/**
		 * @return the preferred width of this ui-element
		 */
		virtual gsize_t getPreferredWidth() const {
			return _prefWidth ? _prefWidth : getMinWidth();
		};
		/**
		 * @return the preferred height of this ui-element
		 */
		virtual gsize_t getPreferredHeight() const {
			return _prefHeight ? _prefHeight : getMinHeight();
		};

		/**
		 * @return the theme of this ui-element
		 */
		inline Theme &getTheme() {
			return _theme;
		};
		inline const Theme &getTheme() const {
			return _theme;
		};

		/**
		 * Performs the layout-calculation for this ui-element. This is only used by Window and
		 * Panel.
		 */
		virtual void layout() = 0;

		/**
		 * Repaints the control, i.e. calls paint()
		 *
		 * @param update whether to request an update of the painted region
		 */
		void repaint(bool update = true);

		/**
		 * Paints the given rectangle of the control
		 *
		 * @param g the graphics-object
		 * @param x the x-position relative to this control of the rectangle
		 * @param y the y-position relative to this control of the rectangle
		 * @param width the width of the rectangle
		 * @param height the height of the rectangle
		 */
		virtual void paintRect(Graphics &g,gpos_t x,gpos_t y,gsize_t width,gsize_t height);
		/**
		 * Repaints the given rectangle of the control, i.e. calls paintRect() and requests an
		 * update of this rectangle.
		 */
		void repaintRect(gpos_t x,gpos_t y,gsize_t width,gsize_t height);

		/**
		 * Requests an update of the dirty region
		 */
		void requestUpdate();

		/**
		 * @return the graphics-object
		 */
		inline Graphics *getGraphics() const {
			return _g;
		};

		/**
		 * @return the window this ui-element belongs to
		 */
		Window *getWindow();
		/**
		 * @return the parent-element (may be NULL if not added to a panel yet or its a window)
		 */
		inline UIElement *getParent() {
			return _parent;
		};

		/**
		 * The event senders
		 */
		inline mouseev_type &mouseMoved() {
			return _mouseMoved;
		};
		inline mouseev_type &mousePressed() {
			return _mousePressed;
		};
		inline mouseev_type &mouseReleased() {
			return _mouseReleased;
		};
		inline mouseev_type &mouseWheel() {
			return _mouseWheel;
		};
		inline keyev_type &keyPressed() {
			return _keyPressed;
		};
		inline keyev_type &keyReleased() {
			return _keyReleased;
		};

		/**
		 * The callback-methods for the events
		 *
		 * @param e the event
		 */
		virtual void onMouseMoved(const MouseEvent &e);
		virtual void onMouseReleased(const MouseEvent &e);
		virtual void onMousePressed(const MouseEvent &e);
		virtual void onMouseWheel(const MouseEvent &e);
		virtual void onKeyPressed(const KeyEvent &e);
		virtual void onKeyReleased(const KeyEvent &e);

		/**
		 * @return whether calls of repaint() actually perform a repaint
		 */
		inline bool isRepaintEnabled() const {
			return _enableRepaint;
		};
		/**
		 * Sets whether calls of repaint() actually perform a repaint. That is, by setting it to
		 * false, calls of repaint() are ignored.
		 *
		 * @param en the new value
		 */
		inline void setRepaintEnabled(bool en) {
			_enableRepaint = en;
		};

	protected:
		/**
		 * Does the actual painting and has to be implemented by subclasses.
		 *
		 * @param g the graphics object
		 */
		virtual void paint(Graphics &g) = 0;

		/**
		 * @return the minimum width that the ui-element should have to be displayed in a
		 * 	reasonable way
		 */
		virtual gsize_t getMinWidth() const = 0;
		/**
		 * @return the minimum height that the ui-element should have to be displayed in a
		 * 	reasonable way
		 */
		virtual gsize_t getMinHeight() const = 0;

		/**
		 * Sets the parent of this control (used by Panel)
		 *
		 * @param e the parent
		 */
		virtual void setParent(UIElement *e) {
			_parent = e;
		};

	private:
		// I've decided to make all ui-elements not-clonable for the following reasons:
		// 1. I can't think of a real reason why cloning is necessary. Of course, one could argue
		//    that its sometimes convenient if objects are similar, but is there really a situtation
		//    in which I can't live without it?
		// 2. It makes it more complicated and error-prone.
		// 3. It introduces the problem that objects of various sub-classes need to be clonable
		//    without knowing their real type. That is, e.g. panels that manage a list of controls
		//    would need to clone all these controls, but don't know their real-type. Therefore we
		//    would need an additional method like "virtual UIElement *clone() const", that needs
		//    to be overwritten by every subclass.
		// 4. Sometimes its not clear what cloning an ui-element should actually mean. For example,
		//    lets say I have a panel with a borderlayout that has a button on it at any position.
		//    Now I'm cloning the button. What should happen? Should the clone be put in the same
		//    slot in the borderlayout? (and overwrite the original) Or be put in another slot? What
		//    if all slots are in use? -> There is no clear and intuitive way to clone this button.
		// So, all in all, it doesn't help much and is not required, but would bring a lot of
		// trouble. Means, better don't allow it at all :)
		UIElement(const UIElement &e);
		UIElement &operator=(const UIElement &e);

		/**
		 * Sets the x-position
		 *
		 * @param x the new position
		 */
		inline void setX(gpos_t x) {
			_x = x;
		};
		/**
		 * Sets the y-position
		 *
		 * @param y the new position
		 */
		inline void setY(gpos_t y) {
			_y = y;
		};
		/**
		 * Sets the width
		 *
		 * @param width the new width
		 */
		inline void setWidth(gsize_t width) {
			_width = width;
		};
		/**
		 * Sets the height
		 *
		 * @param height the new height
		 */
		inline void setHeight(gsize_t height) {
			_height = height;
		};

		/**
		 * Informs the ui-element that the child c currently has the focus
		 *
		 * @param c the child-control
		 */
		virtual void setFocus(Control *c);

		/**
		 * Adds some debugging info after drawing an UIElement
		 */
		void debug();

	private:
		id_type _id;
		Graphics *_g;
		UIElement *_parent;
		Theme _theme;
		gpos_t _x;
		gpos_t _y;
		gsize_t _width;
		gsize_t _height;
		mouseev_type _mouseMoved;
		mouseev_type _mousePressed;
		mouseev_type _mouseReleased;
		mouseev_type _mouseWheel;
		keyev_type _keyPressed;
		keyev_type _keyReleased;
		bool _enableRepaint;
		static id_type _nextid;
	protected:
		gsize_t _prefWidth;
		gsize_t _prefHeight;
	};
}
