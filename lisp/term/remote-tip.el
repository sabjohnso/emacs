;;; remote-tip.el --- tooltips of the remote display  -*- lexical-binding: t -*-

;; Copyright (C) 2026 Free Software Foundation, Inc.

;; This file is part of GNU Emacs.

;; GNU Emacs is free software: you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation, either version 3 of the License, or
;; (at your option) any later version.

;; GNU Emacs is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GNU Emacs.  If not, see <https://www.gnu.org/licenses/>.

;;; Commentary:

;; A tooltip of the remote display is a child frame of the root frame
;; under the mouse.  The client composites a child frame into the
;; window of its root, so a tooltip cannot go outside that window.
;; The tooltip has the frame parameter `remote-tip', so its parent gets
;; the input of the mouse on it (see remoteinput.c).
;;
;; `x-show-tip' makes a new tooltip frame each time, and
;; `remote-display--hide-tip' deletes it.  The C function `x-hide-tip'
;; calls `remote-display--hide-tip', because menu.c calls `x-hide-tip'
;; from C.  This file adapts `tty-tip--create-frame' of tty-tip.el.

;;; Code:

(defvar x-max-tooltip-size '(80 . 40)
  "The largest size of a tooltip: (COLUMNS . LINES).
A text that is larger is clipped.")

(defconst remote-display--tip-default-timeout 5
  "The number of seconds that a tooltip shows when TIMEOUT is nil.")

(defconst remote-display-tip-default-offsets '(5 . 10)
  "The offsets (DX . DY) of a tooltip from the mouse when DX or DY is nil.")

(defvar remote-display--tip-frame nil
  "The frame of the tooltip that shows now, or nil.")

(defvar remote-display--tip-timer nil
  "The timer that hides the tooltip, or nil.")

(defun x-show-tip (string &optional frame parms timeout dx dy)
  "Show STRING in a tooltip near the mouse, for TIMEOUT seconds.
The tooltip is a child frame of the root frame under the mouse.  If
the session of FRAME has no mouse position, the tooltip goes to the
root of FRAME, and the mouse position is the top-left corner of that
root.  FRAME nil means the selected frame.

PARMS are more frame parameters of the tooltip.  An integer `left' or
`top' in PARMS gives the position of the tooltip in its root frame.
Else the tooltip is DX pixels to the right of the mouse and DY pixels
below it, or on the other side of the mouse if it does not fit there.
DX and DY must be positive or nil, and nil gives the values of
`remote-display-tip-default-offsets'.  TIMEOUT nil means 5 seconds.

A text larger than `x-max-tooltip-size' is clipped.  Return nil."
  (remote-display--hide-tip)
  (pcase-let* ((`(,root . ,mouse)
                (remote-display--tip-mouse (or frame (selected-frame))))
               (tip (remote-display--make-tip-frame root parms)))
    (remote-display--fill-tip tip string)
    (remote-display--fit-tip tip)
    (remote-display--place-tip tip root mouse (cons dx dy) parms)
    (make-frame-visible tip)
    (setq remote-display--tip-frame tip)
    (setq remote-display--tip-timer
          (run-with-timer (or timeout remote-display--tip-default-timeout)
                          nil #'remote-display--hide-tip))
    nil))

(defun remote-display--hide-tip ()
  "Delete the tooltip frame.
Return t if a tooltip frame was live, and nil otherwise.  The C
function `x-hide-tip' calls this function."
  (when remote-display--tip-timer
    (cancel-timer remote-display--tip-timer)
    (setq remote-display--tip-timer nil))
  (let ((tip remote-display--tip-frame))
    (setq remote-display--tip-frame nil)
    (when (frame-live-p tip)
      (delete-frame tip)
      t)))

;;;; The place of the tooltip

(defun remote-display--place-tip (tip root mouse offsets parms)
  "Move the tooltip frame TIP to its place in its parent ROOT.
MOUSE, OFFSETS and PARMS are as for `remote-display-tip-position'."
  (pcase-let ((`(,x . ,y)
               (remote-display-tip-position
                mouse
                (cons (frame-pixel-width tip) (frame-pixel-height tip))
                (cons (frame-native-width root) (frame-native-height root))
                offsets
                parms)))
    (set-frame-position tip x y)))

(defun remote-display-tip-position (mouse size root-size offsets parms)
  "Return the position (X . Y) of a tooltip in its root frame.
MOUSE is the position (X . Y) of the mouse in the root frame.  SIZE is
the pixel size (WIDTH . HEIGHT) of the tooltip, and ROOT-SIZE is that
of the root frame.  OFFSETS is (DX . DY), the distances of the tooltip
from the mouse; nil gives the value of
`remote-display-tip-default-offsets'.  An integer `left' or `top' in
the alist PARMS gives the position on that axis."
  (cons (remote-display--tip-axis
         (car mouse) (car size) (car root-size)
         (or (car offsets) (car remote-display-tip-default-offsets))
         (alist-get 'left parms))
        (remote-display--tip-axis
         (cdr mouse) (cdr size) (cdr root-size)
         (or (cdr offsets) (cdr remote-display-tip-default-offsets))
         (alist-get 'top parms))))

(defun remote-display--tip-axis (mouse size limit offset explicit)
  "Return the start of a tooltip of SIZE on one axis of a root of LIMIT.
If EXPLICIT is an integer, return it.  Else put the tooltip OFFSET
after MOUSE if it fits in the root there, or OFFSET before MOUSE if
it fits there.  Else put it at the far edge of the root, or at 0 if
it is larger than the root."
  (let ((after (+ mouse offset))
        (before (- mouse offset size))
        (last (- limit size)))
    (cond ((integerp explicit) explicit)
          ((<= 0 after last) after)
          ((<= 0 before last) before)
          (t (max 0 last)))))

(defun remote-display--tip-mouse (frame)
  "Return (ROOT . POSITION): the root frame and the mouse position in it.
ROOT is the root of the frame under the mouse.  If the session of FRAME
has no mouse position, ROOT is the root of FRAME, and POSITION is
\(0 . 0).  The mouse position of a session is always on a live frame
of that session, because remoteterm.c forgets the mouse frame when it
deletes that frame."
  (pcase-let ((`(,under ,x . ,y) (with-selected-frame frame
                                   (mouse-pixel-position))))
    (if x
        (cons (frame-root-frame under)
              (remote-display--root-position under x y))
      (cons (frame-root-frame frame) '(0 . 0)))))

(defun remote-display--root-position (frame x y)
  "Return the position (X . Y) in the root frame of the point X, Y of FRAME.
Add the position of each frame from FRAME up to its root, because the
position of a child frame is relative to its parent."
  (while (frame-parent frame)
    (let ((position (frame-position frame)))
      (setq x (+ x (car position))
            y (+ y (cdr position))
            frame (frame-parent frame))))
  (cons x y))

;;;; The frame of the tooltip

(defun remote-display--make-tip-frame (root parms)
  "Return a new invisible tooltip frame, a child of ROOT, with PARMS.
The values of `remote-display--tip-parameters' win over PARMS, and the
frame defaults do not apply."
  (let ((after-make-frame-functions nil)
        (default-frame-alist nil)
        (window-system-default-frame-alist nil))
    (make-frame (append (remote-display--tip-parameters root) parms))))

(defun remote-display--tip-parameters (root)
  "Return the frame parameters of a tooltip of ROOT.
The size is one column and one line more than `x-max-tooltip-size', so
the text of that size does not wrap when the tooltip measures it.  The
tooltip has no fringes, so `no-special-glyphs' is necessary: without
it, the last column of the text area holds the continuation glyph, and
a line as wide as the text area wraps.  The bar parameters are
necessary, because a remote frame gets bars from the bar modes when
its parameters do not give them."
  `((display . ,(terminal-name (frame-terminal root)))
    (parent-frame . ,root)
    (minibuffer . ,(minibuffer-window root))
    (remote-tip . t)
    (visibility . nil)
    (width . ,(1+ (car x-max-tooltip-size)))
    (height . ,(1+ (cdr x-max-tooltip-size)))
    (min-width . 1)
    (min-height . 1)
    (no-accept-focus . t)
    (undecorated . t)
    (no-other-frame . t)
    (unsplittable . t)
    (cursor-type . nil)
    (left-fringe . 0)
    (right-fringe . 0)
    (no-special-glyphs . t)
    (menu-bar-lines . 0)
    (tab-bar-lines . 0)
    (tool-bar-lines . 0)
    (vertical-scroll-bars . nil)
    (horizontal-scroll-bars . nil)
    (fullscreen . nil)))

(defun remote-display--fill-tip (tip string)
  "Show STRING in the window of the tooltip frame TIP.
The buffer of the window has no mode line, header line or tab line."
  (let ((buffer (get-buffer-create " *remote-tip*")))
    (with-current-buffer buffer
      (let ((inhibit-read-only t)
            (inhibit-modification-hooks t))
        (erase-buffer)
        (insert string)
        (goto-char (point-min)))
      (setq-local mode-line-format nil
                  header-line-format nil
                  tab-line-format nil
                  truncate-lines nil))
    (let ((window (frame-root-window tip)))
      (set-window-buffer window buffer)
      (set-window-dedicated-p window t))))

(defun remote-display--fit-tip (tip)
  "Give the text area of the tooltip frame TIP the size of its text.
The size is not larger than `x-max-tooltip-size'."
  (let* ((max-width (* (car x-max-tooltip-size) (frame-char-width tip)))
         (max-height (* (cdr x-max-tooltip-size) (frame-char-height tip)))
         (size (window-text-pixel-size (frame-root-window tip) nil nil
                                       max-width max-height)))
    (set-frame-size tip (max 1 (car size)) (max 1 (cdr size)) t)))

(provide 'term/remote-tip)

;;; remote-tip.el ends here
