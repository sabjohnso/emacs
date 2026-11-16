;;; remoteterm-tests.el --- tests for src/remoteterm.c  -*- lexical-binding:t -*-

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

;; Tests for the remote display terminal.  A remote terminal exists
;; without a connected client, so these tests need no client.  The
;; tests use the test clients of src/remotetest.c, so they run only in
;; a build configured with --enable-remote-display-tests.  The laws:
;;
;; 1. Sessions: after any sequence of opens and closes,
;;    `x-display-list' is the set of names that are open.  An open of
;;    an open name changes nothing.  A close of a name that is not
;;    open is an error, and changes nothing.
;; 2. Frame size: a frame made with width W and height H has W columns
;;    and H lines.
;; 3. Colors: for each "#RRGGBB" spec, `xw-color-values' gives each
;;    component times 257.  A string names a color if and only if it
;;    has color values.
;; 4. Drawing: a frame draws a text with the glyph codes that the
;;    font of the frame gives for the text.
;; 5. Frames: the messages of two frames of a session have different
;;    frame IDs.
;; 6. Position: the glyphs of a line of text start at the left edge
;;    of the text area, on the baseline of the first line.  Each run
;;    of glyphs starts where the run before it ends.  The first glyph
;;    of a face with a box starts after the left line of the box, if
;;    that line is outside the glyph.
;; 7. Clear: after a line becomes shorter, replay the messages in
;;    order on the columns of the row.  Each column of the new text
;;    has its glyph, and each column after it is cleared or has a
;;    space.  Each column of a tab is cleared, and a clear for a tab
;;    stays in the area of its row, also on a line that is visible in
;;    part and on a line that scrolled to the left.
;; 8. Scroll: after a change of the start of a window, a replay of
;;    all messages in order shows the lines from the new start.
;; 9. Scroll bounds: for each redisplay, the destination of each
;;    copy-area message is inside the text area of one window after the
;;    redisplay, so a copy does not change a mode line, a header line, a
;;    tab line, or another window.  The source is inside the text area
;;    of that window before the redisplay, and it is above the bottom
;;    of the text area after the redisplay.  This holds for each layout
;;    and each kind of change of `remoteterm-tests--scroll-layouts' and
;;    `remoteterm-tests--scroll-changes', and for random layouts and
;;    changes.  A change of the start by fewer lines than the text area
;;    has sends at least one copy-area message.
;; 10. Cursor: the last draw-cursor message has the kind and the
;;     rectangle that `cursor-type' gives at the position of point.  A
;;     bar is as tall as the row, and an hbar is at the bottom of the
;;     row, also with line spacing.  A cursor on a right-to-left glyph
;;     is at the right edge of the glyph.  The level of the glyph sets
;;     this, not the direction of the paragraph.  A cursor stays in the
;;     row and in the text area, and a cursor with no visible part sends
;;     no message, also if it only touches the visible part.  A column
;;     with no glyph on a short line sends no message.  The
;;     `cursor-type' frame parameter gives the cursor of a buffer whose
;;     `cursor-type' is t.  A window that is not selected, and each
;;     window of a frame with no focus, shows the cursor that
;;     `cursor-in-non-selected-windows' gives.
;; 11. Cursor erase: after point moves, a message covers the rectangle
;;     of the old cursor, for each type of glyph under the cursor.
;; 12. Fringe: a line with a fringe bitmap sends the rows of the
;;     bitmap, in the middle of its fringe, at the top, the center, or
;;     the bottom of the line.  This message has no clip, so it
;;     holds only the rows inside the visible part of the line.  A
;;     plain bitmap is not a cursor and not an overlay.
;; 13. Fringe erase: after the bitmap goes away, a clear-area message
;;     covers its rectangle, also if its window was deleted and another
;;     window grew over it.  An overlay bitmap keeps the pixels under
;;     it: it has the overlay flag, and no clear goes before it.
;; 14. Fringe cursor: a cursor at the end of a line as wide as the
;;     window is a draw-fringe message in the right fringe.  If the
;;     paragraph goes from right to left, it is in the left fringe.
;;     A shorter line sends no fringe cursor, and a frame with no focus
;;     has a hollow fringe cursor.
;; 15. Large frame: the positions and the sizes beyond 16 bits of
;;     glyphs, clears, cursors, and fringe bitmaps reach the client
;;     without a change.  The same is true for copies, after the clip
;;     of law 28.
;; 16. Empty rectangle: a clear, a scroll, or a cursor with a width or
;;     a height of 0 or less sends no message.  A size above 0 and a
;;     position reach the client without a change.
;; 17. Fringe size: a fringe bitmap that has no pixels, or that is too
;;     large for a draw-fringe message, sends no message.  A negative
;;     row offset removes that number of rows, which have no pixels.
;;     The rows of a message start at the row offset of the bitmap.
;; 18. Frame IDs: the IDs of the live frames of a session are distinct,
;;     are not 0, and are unsigned 32-bit integers, for each state of
;;     the counter.  The frames of a session do not change the IDs of
;;     another session.
;; 19. Narrow fringe: a fringe narrower than a bitmap shows the left
;;     pixels of the bitmap, so the rows lose their right pixels.
;; 20. Periodic fringe: each row of a periodic bitmap at the pixel
;;     position Y is the row of the period at Y modulo the period.  The
;;     first row is less than one period below the top of the visible
;;     part of the line, also if the line starts above the frame.
;; 21. Composition: each glyph of a composition reaches the client at
;;     the pen position of the composition plus the offset of the glyph.
;; 22. Sink: "attached" means that a session has a sink.  A session
;;     writes each message to its sink, and without a sink it sends
;;     nothing and its redisplay continues.  Two kinds of sink get the
;;     same bytes.  Each session has its own sink.  After each update
;;     and after a flush of the display, a sink keeps no bytes.
;; 23. Sink lifetime: after any sequence of attaches, detaches, and
;;     closes, the number of live sinks is the number of sessions that
;;     have a sink.  A new sink frees the last one, and a close frees
;;     the sink of the session.
;; 24. Frame memory: a delete of a frame frees its faces, and a frame
;;     that fails while it is made frees them too.  The memory of the
;;     process does not grow with the number of frames that went away,
;;     and malloc holds no more bytes than it held before them.
;; 25. Focus: after a delete of the frame with the focus, the session
;;     has no focus frame and no highlight frame.  A delete of another
;;     frame keeps them.
;; 26. Mouse highlight: after a delete of the frame of the mouse
;;     highlight, the session has no such frame.  A delete of another
;;     frame keeps it.
;; 27. Display size: the size of a display is the largest size of the
;;     frames of its session.
;; 28. Scroll clip: a copy loses the rows that are above the top or
;;     below the bottom of the text area, at its destination, and the
;;     rows that are below the bottom at its source.  The distance of
;;     the copy does not change.  A copy with no rows left sends no
;;     message.
;;
;; 29. Glyph clip: each draw-glyphs message has a clip, and it is inside
;;     the text area, the header line, or the mode line of a window.
;;     So a glyph cut at an edge of the text area does not change the
;;     area next to it.
;; 30. Kboards: a close of a session frees its kboard, so the number of
;;     kboards does not grow with the number of closed sessions.
;; 31. Overlaps: a row that redisplay draws again because it goes over
;;     another row sends no clear-area message.  A draw of a column
;;     next to an overhang clears no column to its left.
;; 32. Face colors: the glyphs of a face have the foreground of the
;;     face, and the clear under them has the background of the face.
;;     The glyphs and the clears of the default face have the colors
;;     of the frame.  Each color is #xRRGGBB, with no high bits.
;; 33. Cursor colors: a cursor has the `cursor-color' of the frame, or
;;     the foreground of the frame if that color is the background of
;;     the frame.  After a box, the glyph under it is drawn again in
;;     the background of the frame, and nothing clears the box.  A bar
;;     on a face whose background is the cursor color has the
;;     foreground of the face.  A box on a face that is not the default
;;     face has the colors that `pgtk_set_cursor_gc' gives.
;; 34. Fringe colors: a fringe bitmap has the colors of the `fringe'
;;     face, and the clear of its area has the background of that
;;     face.  A cursor in the fringe has the cursor color.
;; 35. Font definitions: after each attach, a define-font message for
;;     a font comes before the first draw-glyphs message with that
;;     font, and no font has two definitions.  The frames of a session
;;     have the same definitions.
;; 36. Font identity: a definition has the SHA-256 hash of the font
;;     file, the face index in the file, the pixel size, and the size
;;     of the file in bytes.  Two IDs of fonts with a hash do not
;;     have the same hash, index, and size.
;;     A font with no hash has the ID of its file name, index, and size.
;;     A font keeps its ID after a new attach, and after Emacs opens it
;;     again.
;; 37. File hash: the hash of a file is the SHA-256 hash of its bytes,
;;     and the hash of a file that cannot be read is empty.
;;
;; 38. Trials: a test that runs its trials in a daemon fails if a trial
;;     fails, and the failure names the trial.  This is also true if
;;     the daemon crashes in the trial or in the generator of its value.
;;     A failure outside a trial names no trial.  The test also fails
;;     if no trial finishes.  The name of a trial is correct for each
;;     value, also for non-ASCII text and raw bytes.  The daemon
;;     generates the values that a batch Emacs generates, and it runs
;;     the newest form of the file of the test.
;; 39. Daemon garbage: each daemon of a test collects garbage when it
;;     deletes its first frame, so that frame is live and has drawn.
;;     It collects again after the body of the test.  The test fails if
;;     the daemon does not do that last collection, or crashes in it.
;; 40. Font transfer: a want-font message for the hash of a font of the
;;     session gives font-data messages with the bytes of its file, in
;;     order, with the file size as their total.  A second want-font
;;     message for the file while the session sends it adds no bytes.
;;     A want-font message for an unknown hash or a short hash gives
;;     no font-data message, and also a hash of a file that changed
;;     after the definition of its font.  After the last font-data
;;     message, the session defines the font again, and draws the frame
;;     again.
;; 41. Font pace: while the sink of a session reports 65536 bytes or
;;     more that it did not send, the session sends no font data.  When
;;     the transport reports an empty queue, the session sends the rest
;;     of the file.  A new client gets no font data that an old client
;;     asked for.
;; 42. Clipboard from the client: after the clipboard-data messages of
;;     a UTF-8 text, the CLIPBOARD selection of the session is the text,
;;     and Emacs does not own it, also after a kill before it.  The text
;;     can need several messages.  A target that is not a text, such as
;;     TARGETS, gives nil.
;; 43. Clipboard to the client: `gui-set-selection' of CLIPBOARD sends
;;     the text in clipboard-data messages, which join to the UTF-8
;;     text.  Then Emacs owns CLIPBOARD, and the selection is the text.
;;     PRIMARY stays inside Emacs: it sends no message.  A session with
;;     no client keeps its selections.  A kill of a text above the limit
;;     of the protocol does not fail: Emacs keeps the text, and does not
;;     send it.  After a new attach, the rest of a text that a client did
;;     not send complete does not become the selection.
;; 44. Clipboard pace: while the sink of a session reports 65536 bytes or
;;     more that it did not send, a kill sends no clipboard data.  When
;;     the transport reports an empty queue, the session sends the
;;     text.  A second kill before then replaces the first text, so only
;;     the second text goes to the client.
;; 45. Image types: each image type of `remoteterm-tests--image-types'
;;     is available, unless the build was configured without it.
;; 46. Built-in images: a PBM or XPM image of W x H pixels has the size
;;     W x H on a remote frame.
;; 47. Image data: after a redisplay of a remote frame with an image,
;;     the image-data messages of one ID join to a PNG file, and the
;;     size in its IHDR chunk is the size of the image in pixels.
;; 48. Image scale: with :scale 2, the size of the PNG file is two
;;     times the size of the file of the same image with :scale 1.
;; 49. Image before draw: while the sink reports fewer than 65536 bytes
;;     that it did not send, all the image-data messages of a small
;;     image come before the first draw-image message with its ID.
;; 50. Image flush: the image data of an update goes out with the
;;     draws of the update, in one flush.
;; 51. Image once: a second redisplay of the frame sends no image data
;;     for an ID that the client has.  After a new attach, the session
;;     sends the data again.
;; 52. Image pace: while the sink reports 65536 bytes or more that it
;;     did not send, the session sends no image data.  When the
;;     transport reports an empty queue, the session sends the rest,
;;     and the next redisplay draws the image after its data.
;; 53. Image queue: the data of two images of one update go out one
;;     image after the other, also when the queue of the sink lets the
;;     update send only a part of the first image.  The redisplay after
;;     the transfers draws each image after its data.
;; 54. Image limit: an image with more than 8192 pixels on a side sends
;;     no image data and no draw-image message.  An image with 8192
;;     pixels on a side sends its data.
;; 55. Image position: for each slice of an image with a random margin,
;;     ascent and scale, a clear-area message with the face background
;;     comes before the draw-image message.  The clip of the draw is
;;     inside that clear area and inside the image.  The x of the draw
;;     is the left edge of the text area plus the margin.  The clips of
;;     the slices cover the image, with no common pixel.
;; 56. Image in the mode line: the clip of an image in the mode line is
;;     inside the mode line.
;; 57. Image load failure: an image that cannot load sends no
;;     draw-image message.  It sends four clear-area lines of the face
;;     foreground at the edges of the image.
;; 58. Image cursor: a box cursor on an image with a mask draws the
;;     image again after the box, and clears no part of the box.  On
;;     an image with no mask, the cursor is a hollow box.
;; 59. Image forget: after `clear-image-cache' and a redisplay, the
;;     session sends a forget-image message for each ID that the client
;;     has.  No later draw-image message uses one of these IDs.
;; 60. Image ID not used again: over a random sequence of
;;     `insert-image', `image-flush', `clear-image-cache' and
;;     `redisplay', no image-data or draw-image message comes after a
;;     forget-image message with its ID.
;; 61. Canvas refresh: after each refresh of a canvas and a redisplay,
;;     the draw of the canvas uses a new ID, the client gets the data
;;     of that ID, and the client has no other ID of the canvas.
;; 62. Image forget with no client: an image that goes while no client
;;     is attached sends no forget-image message to the next client.
;;     An image that goes after the deletion of its session does not
;;     crash Emacs.
;; M1. Menu bar draw: each draw-glyphs message is inside the rows of
;;     the menu bar or below the top of the windows.  A frame with a
;;     menu bar draws in its rows.  After each change of the number of
;;     menu bar lines, the clear-area messages cover the rows of the
;;     old and of the new menu bar, so no stale menu bar text stays.
;; M2. Menu bar click: a press of button 1 on a menu bar item gives the
;;     prefix event `menu-bar' and then the event of the item, with a
;;     position in the area `menu-bar'.  The release gives no event.
;; M3. Menu entries: `x-popup-menu' on a remote frame sends one
;;     menu-item message for each entry of the menu, in order, and then
;;     one show-menu message.  The entries nest correctly, and have the
;;     serial of the show-menu message.  They have the labels of the
;;     items, the separators, the submenus, the disabled items, the
;;     toggles, the radios, the selected items, and the key texts of
;;     the menu.  The ids of the items are all different.  A label, a
;;     title, or a key text that is too long is cut at the start of a
;;     character.  The show-menu message has the title of the menu, and
;;     the pixel position in the frame that the position argument
;;     gives.  The session flushes these messages, and `x-popup-menu'
;;     returns only after the client answers.
;; M4. Menu choice: `x-popup-menu' returns the selection of the item
;;     that a menu-choice message of the client chose: the keys to the
;;     item in a keymap menu, or the value of the item in a list of
;;     panes.  The session ignores a choice with another serial, and a
;;     choice of an id that is not an enabled item.  After a dismissal,
;;     `x-popup-menu' returns nil for a click position, and signals
;;     `quit' for a list position.  The session sends one hide-menu
;;     message with the serial of the menu, after the show-menu message.
;; M5. Menu pace: while the sink of a session reports 65536 bytes or
;;     more that it did not send, `x-popup-menu' sends no menu-item
;;     message and no show-menu message.  When the transport reports an
;;     empty queue, the session sends the entries in order, as law M3
;;     gives them, and then the show-menu message.  A quit, a throw, or a
;;     detach while the entries wait sends no entries, no show-menu
;;     message, and no hide-menu message.
;;
;; Redisplay does not run in batch mode, so the drawing tests run
;; their frames in a daemon.  A property test runs its loop of trials
;; in the daemon too, with one call of emacsclient.  A call of a hook
;; needs no redisplay, so the tests of laws 16 to 18, 27, 28, and of
;; the copies of law 15, run in batch mode.

;;; Code:

(require 'cl-lib)
(require 'ert)
(require 'map)
(require 'seq)
(eval-and-compile
  (require 'remote-display-generators
           (expand-file-name
            "remote-display-resources/remote-display-generators"
            (file-name-directory (macroexp-file-name))))
  (require 'remote-display-daemon
           (expand-file-name
            "remote-display-resources/remote-display-daemon"
            (file-name-directory (macroexp-file-name)))))

;; A build without the remote window system does not have these
;; functions.  The tests of such a build are skipped.
(declare-function remote-display--clear-area "remotetest.c"
                  (frame x y width height))
(declare-function remote-display--count "remotetest.c" (display))
(declare-function remote-display--counted-bytes "remotetest.c" (display))
(declare-function remote-display--decode "remotefns.c" (bytes &optional start))
(declare-function remote-display--cursor-state "remotetest.c" (window))
(declare-function remote-display--detach "remotetest.c" (display))
(declare-function remote-display--draw-cursor "remotetest.c" (window hpos))
(declare-function remote-display--draw-overlap "remotetest.c" (window))
(declare-function remote-display--draw-fringe "remotetest.c"
                  (window width height offset y row-y visible-height))
(declare-function remote-display--focus "remotetest.c" (frame))
(declare-function remote-display--focus-frames "remotetest.c" (display))
(declare-function remote-display--flushes "remotetest.c" (display))
(declare-function remote-display--flush-display "remotetest.c" (frame))
(declare-function remote-display--kboard-keymap "remotetest.c" (display))
(declare-function remote-display--live-sinks "remotetest.c" ())
(declare-function remote-display--malloc-used "remotetest.c" ())
(declare-function remote-display--mouse-frame "remotetest.c" (display))
(declare-function remote-display--mouse-face-window "remotetest.c" (display))
(declare-function remote-display--record "remotetest.c" (display))
(declare-function remote-display--scroll-run "remotetest.c"
                  (window current-y desired-y height))
(declare-function remote-display--set-last-frame-id "remotetest.c"
                  (display id))
(declare-function remote-display--set-last-image-id "remotetest.c" (id))
(declare-function remote-display--set-max-image-size "remotetest.c" (size))
(declare-function remote-display--png-pixels "remotetest.c" (file))
(declare-function remote-display--take-output "remotetest.c" (display))
(declare-function remote-display--unflushed-bytes "remotetest.c" (display))
(declare-function x-close-connection "remoteterm.c" (terminal))
(declare-function x-create-frame "remoteterm.c" (parms))
(declare-function x-display-list "remoteterm.c" ())
(declare-function x-open-connection "remoteterm.c"
                  (display &optional xrm-string must-succeed))
(declare-function xw-color-defined-p "remoteterm.c" (color &optional frame))
(declare-function xw-color-values "remoteterm.c" (color &optional frame))

(defun remoteterm-tests--terminal (display)
  "Return the live terminal for DISPLAY, or nil."
  (seq-find (lambda (terminal)
              (and (terminal-live-p terminal)
                   (equal (terminal-name terminal) display)))
            (terminal-list)))

(defun remoteterm-tests--close-all ()
  "Close every remote display session."
  (mapc #'x-close-connection (x-display-list)))

(defmacro remoteterm-tests--with-display (display &rest body)
  "Open the remote DISPLAY, evaluate BODY, and then close DISPLAY."
  (declare (indent 1))
  `(progn
     (x-open-connection ,display)
     (unwind-protect
         (progn ,@body)
       (when (remoteterm-tests--terminal ,display)
         (x-close-connection ,display)))))

(defconst remoteterm-tests--plain-layout
  '((menu-bar-lines . 0) (tab-bar-lines . 0) (tool-bar-lines . 0)
    (vertical-scroll-bars . nil))
  "The frame parameters of a frame with no bars.
`remoteterm-tests--make-frame' adds them after the parameters of a
test, so a parameter of the test wins.  A test that needs a bar asks
for it.  A test of the default bars calls `x-create-frame-with-faces'
directly.")

(defun remoteterm-tests--make-frame (display parameters)
  "Make a frame on DISPLAY with PARAMETERS and no bars.
A bar comes only if PARAMETERS ask for it (see
`remoteterm-tests--plain-layout').  `make-frame' makes no graphical
frames in batch mode, so call the function that it calls for
graphical frames."
  (x-create-frame-with-faces
   (cons (cons 'display display)
         (append parameters remoteterm-tests--plain-layout))))

(defmacro remoteterm-tests--with-frame (frame display parameters &rest body)
  "Bind FRAME to a new frame on DISPLAY with PARAMETERS, and evaluate BODY.
Then delete the frame."
  (declare (indent 3))
  `(let ((,frame (remoteterm-tests--make-frame ,display ,parameters)))
     (unwind-protect
         (progn ,@body)
       (delete-frame ,frame))))

;;;; Sessions

(ert-deftest remoteterm-tests-open-connection ()
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-test"
    (should (eq (terminal-live-p (remoteterm-tests--terminal "remote-test"))
                'remote))))

(ert-deftest remoteterm-tests-close-connection ()
  (skip-unless (featurep 'remote-test))
  (x-open-connection "remote-test")
  (x-close-connection "remote-test")
  (should-not (remoteterm-tests--terminal "remote-test")))

(defun remoteterm-tests--session-operation ()
  "Return a random open or close of one of a few session names."
  (list (rd-gen-element '(open close))
        (rd-gen-element '("remote-a" "remote-b" "remote-c"))))

(defun remoteterm-tests--apply (operation open-names)
  "Apply OPERATION to the sessions, and return the new OPEN-NAMES.
OPEN-NAMES is the model: the names that are open before OPERATION."
  (pcase-exhaustive operation
    (`(open ,name)
     (x-open-connection name)
     (if (member name open-names) open-names (cons name open-names)))
    (`(close ,name)
     (if (member name open-names)
         (progn (x-close-connection name) (remove name open-names))
       (should-error (x-close-connection name))
       open-names))))

(ert-deftest remoteterm-tests-sessions ()
  "After each operation, `x-display-list' equals the model."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--close-all)
  (rd-gen-for-all
   (lambda () (rd-gen-list #'remoteterm-tests--session-operation 8))
   (lambda (operations)
     (unwind-protect
         (let ((open-names nil))
           (dolist (operation operations)
             (setq open-names (remoteterm-tests--apply operation open-names))
             (should (equal (sort (copy-sequence (x-display-list)) #'string<)
                            (sort (copy-sequence open-names) #'string<)))))
       (remoteterm-tests--close-all)))))

(ert-deftest remoteterm-tests-close-with-frame ()
  "A session that has a frame stays open."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-test"
    (remoteterm-tests--with-frame frame "remote-test" nil
      (should-error (x-close-connection "remote-test"))
      (should (remoteterm-tests--terminal "remote-test")))))

;;;; Frames

(ert-deftest remoteterm-tests-make-frame ()
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-test"
    (remoteterm-tests--with-frame frame "remote-test"
        '((width . 80) (height . 25))
      (should (eq (framep frame) 'remote))
      (should (display-graphic-p frame))
      (should (= (frame-width frame) 80))
      (should (= (frame-height frame) 25)))))

(ert-deftest remoteterm-tests-frame-size ()
  (skip-unless (featurep 'remote-test))
  (let ((rd-gen-trials 50))
    (remoteterm-tests--with-display "remote-test"
      (rd-gen-for-all
       (lambda () (cons (rd-gen-integer 10 300) (rd-gen-integer 5 100)))
       (lambda (size)
         (remoteterm-tests--with-frame frame "remote-test"
             `((width . ,(car size)) (height . ,(cdr size)))
           (should (= (frame-width frame) (car size)))
           (should (= (frame-height frame) (cdr size)))))))))

(ert-deftest remoteterm-tests-set-frame-size ()
  "`set-frame-size' gives the frame the text size that it asks for."
  (skip-unless (featurep 'remote-test))
  (let ((rd-gen-trials 30))
    (remoteterm-tests--with-display "remote-test"
      (remoteterm-tests--with-frame frame "remote-test" nil
        (rd-gen-for-all
         (lambda () (list (rd-gen-integer 100 2000) (rd-gen-integer 100 2000)))
         (lambda (size)
           (set-frame-size frame (nth 0 size) (nth 1 size) t)
           (should (equal (list (frame-text-width frame)
                                (frame-text-height frame))
                          size))))))))

(ert-deftest remoteterm-tests-frame-colors ()
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-test"
    (remoteterm-tests--with-frame frame "remote-test"
        '((foreground-color . "red") (background-color . "#102030"))
      (should (equal (frame-parameter frame 'foreground-color) "red"))
      (should (equal (frame-parameter frame 'background-color) "#102030"))
      (should (equal (face-attribute 'default :foreground frame) "red")))))

(ert-deftest remoteterm-tests-frame-visibility ()
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-test"
    (remoteterm-tests--with-frame frame "remote-test" nil
      (should (frame-visible-p frame))
      (make-frame-invisible frame t)
      (should-not (frame-visible-p frame))
      (make-frame-visible frame)
      (should (frame-visible-p frame)))))

(ert-deftest remoteterm-tests-scroll-bar-mode ()
  "An init file can turn `scroll-bar-mode' off and on in a remote build."
  (skip-unless (featurep 'remote-test))
  (should (fboundp 'scroll-bar-mode))
  (let ((mode scroll-bar-mode)
        (alist default-frame-alist))
    (unwind-protect
        (remoteterm-tests--with-display "remote-test"
          (remoteterm-tests--with-frame frame "remote-test"
              '((scroll-bar-width . 14))
            (with-selected-frame frame
              (scroll-bar-mode -1)
              (should-not scroll-bar-mode)
              (scroll-bar-mode 1)
              (should scroll-bar-mode)
              (should (= (window-scroll-bar-width (frame-root-window frame))
                         14)))))
      (setq scroll-bar-mode mode
            default-frame-alist alist))))

(ert-deftest remoteterm-tests-scroll-bar-area ()
  "Law S1: a remote window has the vertical scroll-bar area of its request.
When a frame starts and after each change of the scroll-bar
parameters, each window has an area of the configured width on the
requested side, or no area if the request is nil.  t requests the
side of `default-frame-scroll-bars'.  A window has no horizontal
scroll bar, because only toolkit builds have one."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-test"
    (let ((rd-gen-trials 16))
      (rd-gen-for-all
       (lambda ()
         (cons (rd-gen-element (remoteterm-tests--scroll-bar-requests))
               (rd-gen-integer 1 40)))
       (lambda (case)
         (pcase-let ((`(,bars . ,width) case))
           (remoteterm-tests--with-frame frame "remote-test"
               `(,@bars (scroll-bar-width . ,width) (scroll-bar-height . 14))
             (split-window (frame-root-window frame))
             (remoteterm-tests--should-have-scroll-bar-area frame bars width)
             (dolist (other (remoteterm-tests--scroll-bar-requests))
               (modify-frame-parameters frame other)
               (remoteterm-tests--should-have-scroll-bar-area
                frame other width)))))))))

(ert-deftest remoteterm-tests-scroll-bar-default-width ()
  "Law S2: the default scroll-bar width is whole columns of at least 14 pixels.
When `scroll-bar-width' is nil, the width of the area is the smallest
multiple of the column width that is at least 14, as for an X scroll
bar with no toolkit."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-test"
    (let ((rd-gen-trials 12))
      (rd-gen-for-all
       (lambda ()
         (list (rd-gen-element '(left right))
               (format "Monospace-%d" (rd-gen-integer 4 30))))
       (lambda (case)
         (pcase-let ((`(,side ,font) case))
           (remoteterm-tests--with-frame frame "remote-test"
               `((vertical-scroll-bars . ,side) (font . ,font))
             (let ((column (frame-char-width frame)))
               (should (equal (window-scroll-bar-width
                               (frame-root-window frame))
                              (* column (ceiling 14 column))))))))))))

(defun remoteterm-tests--scroll-bar-requests ()
  "Return each combination of the two scroll-bar frame parameters."
  (mapcan (lambda (vertical)
            (mapcar (lambda (horizontal)
                      `((vertical-scroll-bars . ,vertical)
                        (horizontal-scroll-bars . ,horizontal)))
                    '(nil t)))
          '(nil t left right)))

(defun remoteterm-tests--should-have-scroll-bar-area (frame request width)
  "Check that each window of FRAME has the scroll-bar area of REQUEST.
REQUEST is an alist of the two scroll-bar parameters.  WIDTH is the
configured width of a vertical scroll bar."
  (let* ((vertical (alist-get 'vertical-scroll-bars request))
         (side (if (eq vertical t) default-frame-scroll-bars vertical)))
    (dolist (window (window-list frame 'never))
      (should (equal (list request (window-scroll-bar-width window)
                           (window-scroll-bar-height window)
                           (car (window-current-scroll-bars window)))
                     (list request (if side width 0) 0 side))))))

(ert-deftest remoteterm-tests-scroll-bar-default ()
  "A remote frame has the scroll bars of `default-frame-scroll-bars'.
A frame with no vertical-scroll-bars parameter gets the default of
Emacs for this window system."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-test"
    (let ((frame (x-create-frame-with-faces '((display . "remote-test")))))
      (unwind-protect
          (should (eq (frame-parameter frame 'vertical-scroll-bars)
                      default-frame-scroll-bars))
        (delete-frame frame)))))

(rd-daemon-deftest remoteterm-tests-scroll-bar-draw
  "Law S3: a redisplay draws the full scroll-bar column of each window.
The clear-area messages cover the column from the top edge of the
window to its bottom edge.  In the rows of the text area, the handle
has the frame foreground, and the trough and the border have the frame
background.  The handle top in the range of
the bar agrees with the window start in the buffer, within 1 pixel."
  (let ((rd-gen-trials 30))
    (rd-gen-for-all
     #'remoteterm-tests--scroll-bar-case
     (lambda (case)
       (pcase-let (((map :output :columns)
                    (rd-daemon-eval
                     (remoteterm-tests--scroll-bar-form case nil)))
                   (`(,fg ,bg) (mapcar #'remoteterm-tests--rgb-pixel
                                       (plist-get case :colors))))
         (dolist (column columns)
           (remoteterm-tests--should-draw-scroll-bar output column fg bg)))))))

(rd-daemon-deftest remoteterm-tests-scroll-bar-split
  "A split of a window draws the full scroll-bar column of each window.
With left scroll bars, `split-window-right' puts the column of the new
window in the middle of the frame.  A clear covers each pixel of that
column, also beside the mode line of the new window."
  (let ((rd-gen-trials 10))
    (rd-gen-for-all
     (lambda ()
       (plist-put (remoteterm-tests--scroll-bar-case) :side 'left))
     (lambda (case)
       (pcase-let (((map :output :columns)
                    (rd-daemon-eval
                     (remoteterm-tests--scroll-bar-form
                      case '(split-window-right))))
                   (`(,fg ,bg) (mapcar #'remoteterm-tests--rgb-pixel
                                       (plist-get case :colors))))
         (should (= (length columns) 2))
         (dolist (column columns)
           (remoteterm-tests--should-draw-scroll-bar
            output column fg bg)))))))

(defun remoteterm-tests--scroll-bar-case ()
  "Return a random case for the tests of the scroll-bar draw.
The value is a plist.  :side is the side of the bars, :lines the
number of lines of the buffer, :start the first line in the window,
:mode-line, :header-line and :tab-line the formats of these lines,
and :colors the foreground and the background of the frame.  The two
colors are different."
  (let ((lines (rd-gen-integer 0 200))
        (fg (remoteterm-tests--rgb))
        (bg (remoteterm-tests--rgb)))
    (list :side (rd-gen-element '(left right))
          :lines lines
          :start (rd-gen-integer 0 lines)
          :mode-line (rd-gen-element '(nil "M"))
          :header-line (rd-gen-element '(nil "H"))
          :tab-line (rd-gen-element '(nil "T"))
          :colors (list (if (equal fg bg)
                            (mapcar (lambda (c) (logxor c 128)) fg)
                          fg)
                        bg))))

(defun remoteterm-tests--scroll-bar-form (case change)
  "Return a form for the daemon that draws a frame with the scroll bars of CASE.
CASE is as for `remoteterm-tests--scroll-bar-case'.  If CHANGE is
non-nil, the form evaluates it in the window after the first draw, and
redisplays again.  The value of the form is a plist.  :output is the
bytes of the last draw, and :columns is the list of the scroll-bar
columns of the windows of the frame, as for
`remoteterm-tests--scroll-bar-column'."
  (pcase-let (((map :side :lines :start :mode-line :header-line :tab-line
                    :colors)
               case))
    (remoteterm-tests--frame-form
     `((width . 40) (height . 20) (vertical-scroll-bars . ,side)
       (foreground-color . ,(remoteterm-tests--rgb-string (nth 0 colors)))
       (background-color . ,(remoteterm-tests--rgb-string (nth 1 colors))))
     `(progn
        (dotimes (line ,lines)
          (insert (format "line %d\n" line)))
        (insert "end")
        (setq mode-line-format ,mode-line
              header-line-format ,header-line
              tab-line-format ,tab-line)
        (goto-char (point-min))
        (forward-line ,start)
        (set-window-start window (point))
        (set-window-point window (point)))
     `(progn
        (when ',change
          (with-selected-window window ,change)
          (redisplay t)
          (setq output (remote-display--take-output display)))
        (list :output output
              :columns (mapcar #'remoteterm-tests--scroll-bar-column
                               (window-list frame 'never)))))))

(defun remoteterm-tests--scroll-bar-column (window)
  "Return the scroll-bar column of WINDOW as a plist.
:x and :width are the pixel columns of the column.  :top and :bottom
are its rows: the top edge of WINDOW, and the bottom edge with no
bottom divider.  :box-top and :box-bottom are the rows of the text
area.  :start is the window start and :whole is the size of the
buffer, both relative to `point-min'."
  (pcase-let ((`(,left ,top ,right ,bottom) (window-pixel-edges window))
              (`(,_ ,box-top ,_ ,box-bottom)
               (window-inside-pixel-edges window))
              (width (window-scroll-bar-width window)))
    (with-current-buffer (window-buffer window)
      (list :x (if (eq (car (window-current-scroll-bars window)) 'left)
                   left
                 (- right (window-right-divider-width window) width))
            :width width
            :top top
            :bottom (- bottom (window-bottom-divider-width window))
            :box-top box-top
            :box-bottom box-bottom
            :start (- (window-start window) (point-min))
            :whole (- (point-max) (point-min))))))

(defun remoteterm-tests--column-pixels (output column)
  "Return the pixels of COLUMN after the clear-area messages of OUTPUT.
COLUMN is as for `remoteterm-tests--scroll-bar-column'.  The value is
a vector of the rows from :top to :bottom of COLUMN, and each row is a
vector of the pixels from :x over :width.  A pixel is the color of the
last clear-area message that covers it, or nil if no message covers
it."
  (pcase-let* (((map :x :width :top :bottom) column)
               (rows (apply #'vector
                            (cl-loop repeat (- bottom top)
                                     collect (make-vector width nil)))))
    (dolist (fields (remoteterm-tests--of-type 'clear-area output) rows)
      (pcase-let (((map (:x area-x) (:y area-y) (:width area-width)
                        (:height area-height) :color)
                   fields))
        (cl-loop for y from (max area-y top)
                 below (min (+ area-y area-height) bottom)
                 do (cl-loop for pixel from (max area-x x)
                             below (min (+ area-x area-width) (+ x width))
                             do (aset (aref rows (- y top)) (- pixel x)
                                      color)))))))

(defun remoteterm-tests--should-draw-scroll-bar (output column fg bg)
  "Check that OUTPUT draws the scroll bar of COLUMN with FG and BG.
COLUMN is as for `remoteterm-tests--scroll-bar-column'.  FG and BG are
the protocol colors of the frame foreground and background.  A clear
covers each pixel of the column.  The mode line, the header line and
the tab line are full-width rows, so redisplay draws them over the
column after the bar, and their pixels can have the color of a face."
  (pcase-let* (((map :width :top :box-top :box-bottom) column)
               (pixels (remoteterm-tests--column-pixels output column))
               (box (cl-subseq pixels (- box-top top) (- box-bottom top))))
    (should (> width 0))
    (should (cl-every (lambda (row) (cl-notany #'null row)) pixels))
    (should (cl-every (lambda (row)
                        (cl-every (lambda (pixel) (memql pixel (list fg bg)))
                                  row))
                      box))
    (remoteterm-tests--should-draw-handle box column fg bg)))

(defun remoteterm-tests--should-draw-handle (box column fg bg)
  "Check the handle in the pixels BOX of the text-area rows of COLUMN.
COLUMN is as for `remoteterm-tests--scroll-bar-column'.  The handle is
the rows with the color FG, which have FG inside the border and BG in
the border.  It has at least 5 rows if the inside of the bar has them.
Its top agrees with the window start, within 1 pixel."
  (pcase-let* (((map :width :box-top :start :whole) column)
               (inside-top (+ box-top 2))
               (inside-height (max 0 (- (length box) 4)))
               (range (max 0 (- inside-height 5)))
               (handle-row (if (> width 4)
                               (vconcat (make-vector 2 bg)
                                        (make-vector (- width 4) fg)
                                        (make-vector 2 bg))
                             (make-vector width bg)))
               (handle (cl-loop for row across box
                                for y from box-top
                                when (memql fg (append row nil))
                                collect y)))
    (dolist (y handle)
      (should (equal (aref box (- y box-top)) handle-row)))
    (should (>= (length handle) (if (> width 4) (min 5 inside-height) 0)))
    (when handle
      (should (equal handle (number-sequence (car handle)
                                             (car (last handle)))))
      (should (<= inside-top (car handle)))
      (should (< (car (last handle)) (+ inside-top inside-height)))
      (should (<= (abs (- (car handle) inside-top
                          (if (zerop whole) 0 (/ (* start range 1.0) whole))))
                  1)))))

;;;; Scroll-bar input

(rd-daemon-deftest remoteterm-tests-scroll-bar-click
  "Law S4: a press on a bar and its release give scroll-bar events.
A press of a button that is not a wheel button, at a random row of the
text area beside a window W, gives a down event at (W
vertical-scroll-bar (OFFSET . RANGE) TIME PART).  OFFSET is the row
relative to the inside top of the bar, clamped to [0, RANGE].  PART is
the part of the drawn bar at the row.  The release of the button gives
a click or a drag event, also if it is on the bar, in the text area of
W, in another window, outside the frame, or in another frame.  The end
position of that event is always on the bar of W, with an OFFSET in
[0, RANGE].  A release in the frame of W has the OFFSET of its row."
  (let ((rd-gen-trials 30))
    (rd-gen-for-all
     #'remoteterm-tests--scroll-bar-click-case
     (lambda (case)
       (pcase-let* (((map :events :down :range :release-offset)
                     (rd-daemon-eval
                      (remoteterm-tests--scroll-bar-click-form case)))
                    (button (plist-get case :button))
                    (`(,press ,release) events))
         (should (= (length events) 2))
         (should (eq (car press) (intern (format "down-mouse-%d" button))))
         (should (equal (nth 1 press) down))
         (should (memq (car release)
                       (list (intern (format "mouse-%d" button))
                             (intern (format "drag-mouse-%d" button)))))
         (pcase-let ((`(,window ,area (,offset . ,end-range) ,_)
                      (nth 2 release)))
           (should (eq window 'w))
           (should (eq area 'vertical-scroll-bar))
           (should (= end-range range))
           (should (<= 0 offset range))
           (when release-offset
             (should (= offset release-offset)))))))))

(defvar remoteterm-tests--bar-time 0
  "The time of the client of the last press on a bar.
The time increases from trial to trial, so that two presses are never
a double click.")

(defun remoteterm-tests--scroll-bar-click-case ()
  "Return a random case for the law of the scroll-bar click.
The value is a plist.  :side is the side of the bars, :lines the
number of lines of the buffer, and :start the first line in the
windows.  The frame has two windows, one above the other.  :window is
0 or 1, the window of the press.  :button is a button that is not a
wheel button.  :press is (FX FY), the position of the press in the
text-area rows of the bar, in thousandths.  :release is (KIND FX FY),
the position of the release.  KIND is `bar', `text', `other-window',
`outside' or `other-frame'."
  (let ((lines (rd-gen-integer 0 200)))
    (list :side (rd-gen-element '(left right))
          :lines lines
          :start (rd-gen-integer 0 lines)
          :window (rd-gen-integer 0 1)
          :button (rd-gen-element '(1 2 3 8 9))
          :press (list (rd-gen-integer 0 999) (rd-gen-integer 0 999))
          :release (list (rd-gen-element
                          '(bar text other-window outside other-frame))
                         (rd-gen-integer 0 999) (rd-gen-integer 0 999)))))

(defun remoteterm-tests--scroll-bar-click-form
    (case &optional run)
  "Return a form for the daemon that clicks on a bar as CASE says.
CASE is as for `remoteterm-tests--scroll-bar-click-case'.  RUN is the
function that sends the input, `remoteterm-tests--scroll-bar-click-run'
if it is nil.  The form calls RUN with the arguments of that function,
and its value is the value of RUN."
  (pcase-let (((map :side :lines :start) case))
    (remoteterm-tests--frame-form
     `((width . 40) (height . 20) (vertical-scroll-bars . ,side)
       (foreground-color . "#000000") (background-color . "#ffffff"))
     `(progn
        (dotimes (line ,lines)
          (insert (format "line %d\n" line)))
        (insert "end")
        (goto-char (point-min))
        (forward-line ,start)
        (dolist (each (list window (split-window window nil 'below)))
          (set-window-start each (point))
          (set-window-point each (point))))
     `(,(or run 'remoteterm-tests--scroll-bar-click-run)
       frame display output ',case))))

(defun remoteterm-tests--scroll-bar-click-run (frame display output case)
  "Send the press and the release of CASE on FRAME of DISPLAY.
OUTPUT is the bytes of the draw of FRAME, with the foreground #000000.
Return a plist.  :events is the summary of each mouse event, as for
`remoteterm-tests--bar-event-summary'.  :down is the start position
that the down event must have.  :range is the range of the bar.
:release-offset is the offset that the release must have, or nil if
the release is in another frame."
  (pcase-let* (((map :window :button :press :release) case)
               (windows (window-list frame 'never))
               (w (nth window windows))
               (column (remoteterm-tests--scroll-bar-column w))
               ((map :x :width :box-top :box-bottom) column)
               (range (max 0 (- box-bottom box-top 4 5)))
               (press-y (+ box-top (remoteterm-tests--thousandths
                                    (nth 1 press) (- box-bottom box-top))))
               (other (and (eq (car release) 'other-frame)
                           (remoteterm-tests--make-frame display nil))))
    (cl-flet ((offset (y) (max 0 (min range (- y box-top 2))))
              (send (frame x y press time)
                (remote-display--input
                 display
                 (remote-display--encode
                  `(button :frame ,(remoteterm-tests--frame-id frame)
                           :time ,time :x ,x :y ,y :button ,button
                           :modifiers 0 :press ,press)))))
      (unwind-protect
          (pcase-let ((`(,release-x . ,release-y)
                       (remoteterm-tests--release-pixel
                        release column w (car (delq w windows)) other))
                      (time (cl-incf remoteterm-tests--bar-time 10000)))
            (send frame (+ x (remoteterm-tests--thousandths
                              (nth 0 press) width))
                  press-y 1 time)
            (send (or other frame) release-x release-y 0 (+ time 50))
            (list :events (mapcar (lambda (event)
                                    (remoteterm-tests--bar-event-summary
                                     event w))
                                  (remoteterm-tests--mouse-events-now))
                  :down (list 'w 'vertical-scroll-bar
                              (cons (offset press-y) range)
                              (remoteterm-tests--bar-part
                               output column press-y))
                  :range range
                  :release-offset (and (not other) (offset release-y))))
        (when other
          (delete-frame other))))))

(defun remoteterm-tests--thousandths (fraction size)
  "Return FRACTION thousandths of SIZE, a number in [0, SIZE)."
  (/ (* fraction size) 1000))

(defun remoteterm-tests--release-pixel (release column w other-window
                                                other-frame)
  "Return the pixel (X . Y) of RELEASE, a release of a click case.
RELEASE is (KIND FX FY) as in `remoteterm-tests--scroll-bar-click-case'.
COLUMN is the scroll-bar column of the window W.  OTHER-WINDOW is the
other window of the frame of W.  OTHER-FRAME is the frame of the
release, if KIND is `other-frame'."
  (pcase-let ((`(,kind ,fx ,fy) release))
    (cl-flet ((in-text (window)
                (pcase-let ((`(,left ,top ,right ,bottom)
                             (window-inside-pixel-edges window)))
                  (cons (+ left (remoteterm-tests--thousandths
                                 fx (- right left)))
                        (+ top (remoteterm-tests--thousandths
                                fy (- bottom top)))))))
      (pcase kind
        ('bar (pcase-let (((map :x :width :top :bottom) column))
                (cons (+ x (remoteterm-tests--thousandths fx width))
                      (+ top (remoteterm-tests--thousandths
                              fy (- bottom top))))))
        ('text (in-text w))
        ('other-window (in-text other-window))
        ('outside (cons (- -1 (* fx 5)) (- (* fy 5) 2500)))
        ('other-frame (in-text (frame-root-window other-frame)))))))

(defun remoteterm-tests--bar-part (output column y)
  "Return the part of the bar of COLUMN at the row Y in the draw OUTPUT.
The handle is the rows of the text area with the color #000000."
  (pcase-let* (((map :top :box-top :box-bottom) column)
               (pixels (remoteterm-tests--column-pixels output column))
               (handle (cl-loop for row from box-top below box-bottom
                                when (memql 0 (append (aref pixels
                                                            (- row top))
                                                      nil))
                                collect row)))
    (cond ((< y (car handle)) 'above-handle)
          ((<= y (car (last handle))) 'handle)
          (t 'below-handle))))

(defun remoteterm-tests--mouse-events-now ()
  "Read the mouse events that wait now, and return them in order."
  (let ((events nil)
        event)
    (while (setq event (read-event nil nil 0.01))
      (when (string-match-p "mouse-" (symbol-name (or (car-safe event)
                                                      'none)))
        (push event events)))
    (nreverse events)))

(defun remoteterm-tests--bar-event-summary (event w)
  "Return the summary (SYMBOL START END) of the mouse EVENT.
START and END are summaries of positions: (WINDOW AREA XY PART), where
WINDOW is `w' for the window W and `other' for another window."
  (cl-flet ((position (posn)
              (list (if (eq (posn-window posn) w) 'w 'other)
                    (nth 1 posn) (nth 2 posn) (nth 4 posn))))
    (list (car event) (position (event-start event))
          (position (event-end event)))))

(defun remoteterm-tests--bar-input-session (parameters function)
  "Call FUNCTION with a drawn frame with PARAMETERS and a second frame.
The frames are on the display \"remote-draw\", which records its
output, so that redisplay draws.  The arguments of
FUNCTION are the frame, its first window, and the second frame.
Return the value of FUNCTION."
  (x-open-connection "remote-draw")
  (let* ((frame (remoteterm-tests--make-frame "remote-draw" parameters))
         (other (remoteterm-tests--make-frame "remote-draw" nil)))
    (unwind-protect
        (progn
          (select-frame frame)
          (remote-display--record "remote-draw")
          (redisplay t)
          (funcall function frame (frame-root-window frame) other))
      (delete-frame frame)
      (delete-frame other))))

(defun remoteterm-tests--bar-click (frame x y button press)
  "Send a press (PRESS is 1) or a release of BUTTON at X, Y of FRAME."
  (remote-display--input
   "remote-draw"
   (remote-display--encode
    `(button :frame ,(remoteterm-tests--frame-id frame)
             :time ,(cl-incf remoteterm-tests--bar-time 10000)
             :x ,x :y ,y :button ,button :modifiers 0 :press ,press))))

(defun remoteterm-tests--bar-pixel (window)
  "Return the pixel (X . Y) at the bottom of the text-area rows of the
scroll bar of WINDOW."
  (pcase-let (((map :x :box-bottom)
               (remoteterm-tests--scroll-bar-column window)))
    (cons (+ x 5) (1- box-bottom))))

(defun remoteterm-tests--text-pixel (window)
  "Return a pixel (X . Y) in the text area of WINDOW."
  (pcase-let ((`(,left ,top . ,_) (window-inside-pixel-edges window)))
    (cons (+ left 3) (+ top 3))))

(defun remoteterm-tests--bar-scroll-case ()
  "Click mouse-1 under the handle, run its binding, return the start.
The value is the start of the window relative to `point-min'."
  (remoteterm-tests--bar-input-session
   '((width . 40) (height . 20) (vertical-scroll-bars . right))
   (lambda (frame window _other)
     (with-temp-buffer
       (dotimes (line 200)
         (insert (format "line %d\n" line)))
       (set-window-buffer window (current-buffer))
       (set-window-start window (point-min))
       (redisplay t)
       (pcase-let ((`(,x . ,y) (remoteterm-tests--bar-pixel window)))
         (remoteterm-tests--bar-click frame x y 1 1)
         (remoteterm-tests--bar-click frame x y 1 0))
       (let ((release (cadr (remoteterm-tests--mouse-events-now))))
         (call-interactively
          (key-binding (vector 'vertical-scroll-bar (car release)))
          nil (vector release))
         (- (window-start window) (point-min)))))))

(ert-deftest remoteterm-tests-scroll-bar-click-scrolls ()
  "A mouse-1 click under the handle moves the window start forward.
`scroll-bar-scroll-up' is the binding of the click."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (> (remoteterm-tests--daemon-call
                'remoteterm-tests--bar-scroll-case)
               0))))

(defun remoteterm-tests--bar-text-click-case ()
  "Click in the text area of a frame with bars, and return the events.
Each event is (SYMBOL AREA PIXEL-P)."
  (remoteterm-tests--bar-input-session
   '((vertical-scroll-bars . left))
   (lambda (frame window _other)
     (pcase-let ((`(,x . ,y) (remoteterm-tests--text-pixel window)))
       (remoteterm-tests--bar-click frame x y 1 1)
       (remoteterm-tests--bar-click frame x y 1 0))
     (mapcar (lambda (event)
               (list (car event) (posn-area (event-start event))
                     (consp (posn-x-y (event-start event)))))
             (remoteterm-tests--mouse-events-now)))))

(ert-deftest remoteterm-tests-scroll-bar-text-click ()
  "A click in the text area of a frame with bars is a plain click."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--bar-text-click-case)
                   '((down-mouse-1 nil t) (mouse-1 nil t))))))

(defun remoteterm-tests--bar-no-layout-case ()
  "Click on the bar of a window that redisplay did not draw yet.
Return the mouse events."
  (x-open-connection "remote-draw")
  (let ((frame (remoteterm-tests--make-frame
                "remote-draw" '((vertical-scroll-bars . right)))))
    (remote-display--record "remote-draw")
    (unwind-protect
        (pcase-let ((`(,x . ,y) (remoteterm-tests--bar-pixel
                                 (frame-root-window frame))))
          (remoteterm-tests--bar-click frame x y 1 1)
          (remoteterm-tests--bar-click frame x y 1 0)
          (mapcar #'car (remoteterm-tests--mouse-events-now)))
      (delete-frame frame))))

(ert-deftest remoteterm-tests-scroll-bar-no-layout ()
  "A button on a bar that redisplay did not draw yet gives no event."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--bar-no-layout-case)
                   nil))))

(defun remoteterm-tests--bar-deleted-frame-case ()
  "Press on a bar, delete its frame, and release on another frame.
Return the mouse events of each step: the press, the release, and
then a click in the text area of the other frame."
  (x-open-connection "remote-draw")
  (let ((frame (remoteterm-tests--make-frame
                "remote-draw" '((vertical-scroll-bars . right))))
        (other (remoteterm-tests--make-frame "remote-draw" nil)))
    (unwind-protect
        (progn
          (select-frame frame)
          (remote-display--record "remote-draw")
          (redisplay t)
          (list
           (pcase-let ((`(,x . ,y) (remoteterm-tests--bar-pixel
                                    (frame-root-window frame))))
             (remoteterm-tests--bar-click frame x y 1 1)
             (mapcar #'car (remoteterm-tests--mouse-events-now)))
           (progn
             (delete-frame frame)
             (remoteterm-tests--bar-click other 3 3 1 0)
             (mapcar #'car (remoteterm-tests--mouse-events-now)))
           (pcase-let ((`(,x . ,y) (remoteterm-tests--text-pixel
                                    (frame-root-window other))))
             (remoteterm-tests--bar-click other x y 1 1)
             (remoteterm-tests--bar-click other x y 1 0)
             (mapcar #'car (remoteterm-tests--mouse-events-now)))))
      (when (frame-live-p frame)
        (delete-frame frame))
      (delete-frame other))))

(ert-deftest remoteterm-tests-scroll-bar-deleted-frame ()
  "A delete of the frame of a press on a bar ends the grab.
The release after it gives no event, and a later click is plain."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--bar-deleted-frame-case)
                   '((down-mouse-1) nil (down-mouse-1 mouse-1))))))

(defun remoteterm-tests--bar-press-again-case ()
  "Press on a bar, then press the same button in the text area and
release it there, with no release between.  Return each mouse event
as (SYMBOL AREA)."
  (remoteterm-tests--bar-input-session
   '((vertical-scroll-bars . left))
   (lambda (frame window _other)
     (pcase-let ((`(,bar-x . ,bar-y) (remoteterm-tests--bar-pixel window))
                 (`(,x . ,y) (remoteterm-tests--text-pixel window)))
       (remoteterm-tests--bar-click frame bar-x bar-y 1 1)
       (remoteterm-tests--bar-click frame x y 1 1)
       (remoteterm-tests--bar-click frame x y 1 0))
     (mapcar (lambda (event)
               (list (car event) (posn-area (event-end event))))
             (remoteterm-tests--mouse-events-now)))))

(ert-deftest remoteterm-tests-scroll-bar-press-again ()
  "A press of the button of a grab, with no release first, ends the grab.
The client can lose a release.  The next press is then a plain press,
and its release is a plain click."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--bar-press-again-case)
                   '((down-mouse-1 vertical-scroll-bar) (down-mouse-1 nil)
                     (mouse-1 nil))))))

;;;; Scroll-bar drag

(rd-daemon-deftest remoteterm-tests-scroll-bar-drag
  "Law S5: while the grab of a bar holds, the motion is on the bar.
After a press of button 2 at a random row of the bar of a window W,
each motion, at a random position of the frame or outside it, gives
\(scroll-bar-movement (W vertical-scroll-bar (OFFSET . RANGE) TIME
PART)) in `track-mouse'.  OFFSET is the row of the motion relative to
the inside top of the bar, clamped to [0, RANGE].  PART is the part of
the drawn bar at the row.  After the release, a motion gives
`mouse-movement' again."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     #'remoteterm-tests--scroll-bar-drag-case
     (lambda (case)
       (pcase-let (((map :moves :after)
                    (rd-daemon-eval
                     (remoteterm-tests--scroll-bar-click-form
                      case 'remoteterm-tests--scroll-bar-drag-run))))
         (dolist (move moves)
           (should (equal (car move) (cadr move))))
         (should (eq after 'mouse-movement)))))))

(defun remoteterm-tests--scroll-bar-drag-case ()
  "Return a random case for the law of the scroll-bar drag.
The value is a case of `remoteterm-tests--scroll-bar-click-case' with
the button 2, and :motions, a list of one to five positions (FX FY).  FX and FY
are thousandths of the width and the height of the frame, from -500
to 1500, so a motion can be outside the frame."
  (append (list :button 2
                :motions (let ((motion (lambda ()
                                         (list (rd-gen-integer -500 1500)
                                               (rd-gen-integer -500 1500)))))
                           (cons (funcall motion)
                                 (rd-gen-list motion 4))))
          (remoteterm-tests--scroll-bar-click-case)))

(defun remoteterm-tests--scroll-bar-drag-run (frame _display output case)
  "Press button 2 on a bar of FRAME, and move as CASE says.
OUTPUT is the bytes of the draw of FRAME, with the foreground #000000.
Return a plist.  :moves is a list of (ACTUAL EXPECTED), one for each
motion.  ACTUAL is the summary of the movement event, as the start of
`remoteterm-tests--bar-event-summary', and EXPECTED is the summary
that the law gives.  :after is the symbol of the movement event of a
motion after the release."
  (pcase-let* (((map :window :press :motions) case)
               (w (nth window (window-list frame 'never)))
               (column (remoteterm-tests--scroll-bar-column w))
               ((map :x :width :box-top :box-bottom) column)
               (range (max 0 (- box-bottom box-top 4 5)))
               (frame-width (frame-pixel-width frame))
               (frame-height (frame-pixel-height frame)))
    (remoteterm-tests--bar-click
     frame (+ x (remoteterm-tests--thousandths (nth 0 press) width))
     (+ box-top (remoteterm-tests--thousandths (nth 1 press)
                                               (- box-bottom box-top)))
     2 1)
    (remoteterm-tests--mouse-events-now)
    (prog1
        (list
         :moves
         (mapcar
          (lambda (motion)
            (let ((y (remoteterm-tests--thousandths (nth 1 motion)
                                                    frame-height)))
              (remoteterm-tests--bar-motion
               frame
               (remoteterm-tests--thousandths (nth 0 motion) frame-width)
               y)
              (let ((event (remoteterm-tests--movement-now)))
                (list (and event
                           (cons (car event)
                                 (cadr (remoteterm-tests--bar-event-summary
                                        event w))))
                      (list 'scroll-bar-movement 'w 'vertical-scroll-bar
                            (cons (max 0 (min range (- y box-top 2)))
                                  range)
                            (remoteterm-tests--bar-part
                             output column y))))))
          motions)
         :after
         (progn
           (remoteterm-tests--bar-click frame 0 0 2 0)
           (remoteterm-tests--mouse-events-now)
           (remoteterm-tests--bar-motion frame 3 3)
           (car (remoteterm-tests--movement-now))))
    (remoteterm-tests--mouse-events-now))))

(defun remoteterm-tests--bar-motion (frame x y)
  "Send a motion of the mouse to X, Y of FRAME."
  (remote-display--input
   "remote-draw"
   (remote-display--encode
    `(motion :frame ,(remoteterm-tests--frame-id frame)
             :time ,(cl-incf remoteterm-tests--bar-time 10000)
             :x ,x :y ,y))))

(defun remoteterm-tests--movement-now ()
  "Read events in `track-mouse' until a movement event, and return it.
Return nil if no movement event comes in a short time."
  (track-mouse
    (let (event)
      (while (and (setq event (read-event nil nil 0.1))
                  (not (memq (car-safe event)
                             '(mouse-movement scroll-bar-movement)))))
      event)))

(defun remoteterm-tests--bar-drag-case ()
  "Drag the handle with mouse-2 down the bar, and then up the bar.
Each drag runs the binding of the press, `scroll-bar-drag'.  The
press is at one end of the bar, the motion is at the other end, and
the release comes later.  Return the window start, relative to
`point-min', after each drag."
  (remoteterm-tests--bar-input-session
   '((width . 40) (height . 20) (vertical-scroll-bars . right))
   (lambda (frame window _other)
     (with-temp-buffer
       (dotimes (line 200)
         (insert (format "line %d\n" line)))
       (set-window-buffer window (current-buffer))
       (set-window-start window (point-min))
       (redisplay t)
       (pcase-let (((map :x :box-top :box-bottom)
                    (remoteterm-tests--scroll-bar-column window)))
         (cl-flet ((drag (from to)
                     (remoteterm-tests--bar-click frame (+ x 5) from 2 1)
                     (let ((down (car (remoteterm-tests--mouse-events-now))))
                       (remoteterm-tests--bar-motion frame (+ x 5) to)
                       (run-with-timer 0.2 nil #'remoteterm-tests--bar-click
                                       frame (+ x 5) to 2 0)
                       (with-timeout (5 nil)
                         (call-interactively
                          (key-binding (vector 'vertical-scroll-bar
                                               (car down)))
                          nil (vector down)))
                       (remoteterm-tests--mouse-events-now)
                       (redisplay t)
                       (- (window-start window) (point-min)))))
           (list (drag box-top (1- box-bottom))
                 (drag (1- box-bottom) box-top))))))))

(ert-deftest remoteterm-tests-scroll-bar-drag-scrolls ()
  "A drag of the handle with mouse-2 moves the window start with it.
A drag down the bar moves the start forward, and a drag up the bar
moves it back."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let ((`(,down ,up) (remoteterm-tests--daemon-call
                               'remoteterm-tests--bar-drag-case)))
      (should (> down 0))
      (should (< up down)))))


(ert-deftest remoteterm-tests-display-pixel-size ()
  "The size of a display is the size of the largest frame of its session."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-test"
    (remoteterm-tests--with-frame small "remote-test"
        '((width . 20) (height . 30))
      (remoteterm-tests--with-frame large "remote-test"
          '((width . 40) (height . 10))
        (should (= (display-pixel-width small) (frame-pixel-width large)))
        (should (= (display-pixel-height large) (frame-pixel-height small)))
        (should (= (display-pixel-width "remote-test")
                   (frame-pixel-width large)))))))

(ert-deftest remoteterm-tests-display-pixel-size-other-session ()
  "The frames of another session do not change the size of a display."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-a"
    (remoteterm-tests--with-display "remote-b"
      (remoteterm-tests--with-frame frame "remote-a" '((width . 20))
        (remoteterm-tests--with-frame other "remote-b" '((width . 40))
          (should (= (display-pixel-width frame)
                     (frame-pixel-width frame))))))))

;;;; Child frames

(defmacro remoteterm-tests--with-child (child parent parameters &rest body)
  "Bind CHILD to a new child frame of PARENT, and evaluate BODY.
PARAMETERS are more parameters of CHILD.  Then delete CHILD."
  (declare (indent 3))
  `(let ((,child (remoteterm-tests--make-child-frame
                  (terminal-name (frame-terminal ,parent))
                  (cons (cons 'parent-frame ,parent) ,parameters))))
     (unwind-protect
         (progn ,@body)
       (delete-frame ,child))))

(defmacro remoteterm-tests--with-parent-and-other (parent other &rest body)
  "Bind PARENT and OTHER to new frames, and evaluate BODY.
PARENT is on the session \"remote-child\", OTHER is on the session
\"remote-other\".  Then delete the frames and close the sessions."
  (declare (indent 2))
  `(remoteterm-tests--with-display "remote-child"
     (remoteterm-tests--with-display "remote-other"
       (remoteterm-tests--with-frame ,parent "remote-child" nil
         (remoteterm-tests--with-frame ,other "remote-other" nil
           ,@body)))))

(rd-daemon-deftest remoteterm-tests-child-frame-parent
  "A frame made with a parent-frame parameter has that parent (law C1).
A parent that is not a live frame of the same session signals an
error, and the session then has no new frame."
  (remoteterm-tests--for-each-case
   '(same-session other-session dead terminal symbol number)
   (lambda (kind)
     (remoteterm-tests--with-parent-and-other parent other
       (let ((candidate (pcase kind
                          ('same-session parent)
                          ('other-session other)
                          ('dead (remoteterm-tests--dead-frame "remote-child"))
                          ('terminal terminal-frame)
                          ('symbol 'parent)
                          ('number 1))))
         (if (eq kind 'same-session)
             (remoteterm-tests--with-child child candidate nil
               (should (eq (frame-parent child) parent)))
           (let ((frames (remoteterm-tests--session-frames "remote-child")))
             (should-error (remoteterm-tests--make-child-frame
                            "remote-child" `((parent-frame . ,candidate))))
             (should (equal (remoteterm-tests--session-frames "remote-child")
                            frames)))))))))

(rd-daemon-deftest remoteterm-tests-child-frame-minibuffer
  "A minibuffer parameter nil or a window gives no minibuffer of its own.
With a window, the frame uses that window (law C2).  A minibuffer
window of another session signals an error."
  (remoteterm-tests--for-each-case
   '(window nil other-session)
   (lambda (kind)
     (remoteterm-tests--with-parent-and-other parent other
       (pcase kind
         ('window
          (let ((window (minibuffer-window parent)))
            (remoteterm-tests--with-child child parent
                `((minibuffer . ,window))
              (should (eq (minibuffer-window child) window))
              (should-not (remoteterm-tests--own-minibuffer-p child)))))
         ('nil
          (remoteterm-tests--with-child child parent '((minibuffer . nil))
            (should-not (remoteterm-tests--own-minibuffer-p child))))
         ('other-session
          (should-error (remoteterm-tests--make-child-frame
                         "remote-child"
                         `((parent-frame . ,parent)
                           (minibuffer . ,(minibuffer-window other)))))))))))

(rd-daemon-deftest remoteterm-tests-frame-visibility-parameter
  "A new frame is visible unless its visibility parameter is nil (law C3).
This holds for a root frame and for a child frame.  The client gets a
draw of the frame only if it is visible."
  (remoteterm-tests--for-each-case
   (mapcan (lambda (kind)
             (mapcar (lambda (visibility) (list kind visibility))
                     '(absent t nil)))
           '(root child))
   (lambda (case)
     (let ((visible (not (null (cadr case)))))
       (should (equal (apply #'remoteterm-tests--visibility-outcome case)
                      (list visible visible)))))))

(rd-daemon-deftest remoteterm-tests-child-frame-no-accept-focus
  "The no-accept-focus parameter of a new frame reads back (law C4)."
  (remoteterm-tests--for-each-case
   '(t nil)
   (lambda (value)
     (remoteterm-tests--with-parent-and-other parent other
       (remoteterm-tests--with-child child parent
           `((no-accept-focus . ,value))
         (should (eq (frame-parameter child 'no-accept-focus) value)))))))

(rd-daemon-deftest remoteterm-tests-child-frame-z-order
  "A new child frame is above each of its siblings (law C5)."
  (let ((rd-gen-trials 10))
    (rd-gen-for-all
     (lambda () (rd-gen-integer 1 5))
     (lambda (count)
       (remoteterm-tests--with-display "remote-child"
         (remoteterm-tests--with-frame parent "remote-child" nil
           (let ((children nil))
             (dotimes (_ count)
               (let ((new (remoteterm-tests--make-child-frame
                           "remote-child" `((parent-frame . ,parent)))))
                 (dolist (old children)
                   (should (frame--z-order-lessp old new))
                   (should-not (frame--z-order-lessp new old)))
                 (push new children))))))))))

(rd-daemon-deftest remoteterm-tests-child-frame-negative-position
  "A negative position of a new child counts from the far edge (law C6).
The left edge of the child is at the native width of the parent minus
the width of the child minus N, for a left value of -N or (- N).  The
top edge is the same with the heights.  A position that is not
negative does not change."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     #'remoteterm-tests--child-position-case
     (lambda (case)
       (pcase-let ((`(,expected ,position)
                    (apply #'remoteterm-tests--child-position case)))
         (should (equal position expected))
         (should (>= (car position) 0))
         (should (>= (cdr position) 0)))))))

(defun remoteterm-tests--child-position-case ()
  "Return a random case of the position law: (LEFT TOP).
Each one is N, -N or (- N), with N a small number of pixels."
  (cl-flet ((offset ()
              (let ((n (rd-gen-integer 0 50)))
                (pcase (rd-gen-random 3)
                  (0 n)
                  (1 (- (1+ n)))
                  (2 `(- ,n))))))
    (list (offset) (offset))))

(defun remoteterm-tests--child-position (left top)
  "Make a child frame at LEFT and TOP, and return (EXPECTED POSITION).
POSITION is the `frame-position' of the child.  EXPECTED is the
position that the law of `remoteterm-tests-child-frame-negative-position'
gives."
  (remoteterm-tests--with-display "remote-child"
    (remoteterm-tests--with-frame parent "remote-child"
        '((width . 60) (height . 30))
      (remoteterm-tests--with-child child parent
          `((width . 10) (height . 4) (left . ,left) (top . ,top))
        (list (cons (remoteterm-tests--edge
                     left (frame-native-width parent)
                     (frame-pixel-width child))
                    (remoteterm-tests--edge
                     top (frame-native-height parent)
                     (frame-pixel-height child)))
              (frame-position child))))))

(defun remoteterm-tests--edge (offset parent-size size)
  "Return the edge that OFFSET gives to a frame of SIZE in PARENT-SIZE."
  (pcase offset
    (`(- ,n) (- parent-size size n))
    ((pred cl-minusp) (+ parent-size (- size) offset))
    (_ offset)))

(defun remoteterm-tests--visibility-outcome (kind visibility)
  "Make a frame of KIND with VISIBILITY, redisplay it, and return the outcome.
KIND is root or child.  VISIBILITY is absent, t or nil.  The value is
\(VISIBLE DRAWN).  VISIBLE is the value of `frame-visible-p' when
`make-frame' returns.  DRAWN is non-nil if the client got a draw of
the frame.  The frame is the selected frame, because a redisplay in
the daemon does not draw the frames that are not selected."
  (let ((display "remote-visible"))
    (remoteterm-tests--with-display display
      (remoteterm-tests--with-frame parent display nil
        (remote-display--record display)
        (let ((frame (remoteterm-tests--make-child-frame
                      display
                      (append (unless (eq visibility 'absent)
                                `((visibility . ,visibility)))
                              (when (eq kind 'child)
                                `((parent-frame . ,parent)))))))
          (unwind-protect
              (let ((visible (frame-visible-p frame)))
                (select-frame frame)
                (redisplay t)
                (list visible
                      (remoteterm-tests--draws-frame-p
                       frame (remote-display--take-output display))))
            (delete-frame frame)))))))

(defun remoteterm-tests--draws-frame-p (frame bytes)
  "Return non-nil if BYTES have a draw message for FRAME."
  (let ((id (remoteterm-tests--frame-id frame)))
    (and (seq-find (lambda (message)
                     (eql (plist-get (cdr message) :frame) id))
                   (remoteterm-tests--draws bytes))
         t)))

(defun remoteterm-tests--make-child-frame (display parameters)
  "Make a frame on DISPLAY with PARAMETERS and no bars, with `make-frame'.
Only a test that runs in a daemon can call this function, because
`make-frame' makes no graphical frames in batch mode.  The test of a
child frame uses `make-frame', because packages make child frames
with it."
  (make-frame (cons (cons 'display display)
                    (append parameters remoteterm-tests--plain-layout))))

(defun remoteterm-tests--dead-frame (display)
  "Return a frame of DISPLAY that was deleted."
  (let ((frame (remoteterm-tests--make-frame display nil)))
    (delete-frame frame)
    frame))

(defun remoteterm-tests--for-each-case (cases check)
  "Call CHECK with each element of CASES, one trial for each case.
A failure names the case, as in `rd-gen-for-all'."
  (let ((rd-gen-trials (length cases))
        (rest cases))
    (rd-gen-for-all (lambda () (pop rest)) check)))

(defun remoteterm-tests--session-frames (display)
  "Return the list of the live frames of the session DISPLAY."
  (seq-filter (lambda (frame)
                (equal (terminal-name (frame-terminal frame)) display))
              (frame-list)))

(defun remoteterm-tests--own-minibuffer-p (frame)
  "Return non-nil if FRAME has a minibuffer window of its own."
  (eq (window-frame (minibuffer-window frame)) frame))

;;;; The places of the frames

(defconst remoteterm-tests--place-display "remote-place"
  "The session of the tests of the places of the frames.")

(defmacro remoteterm-tests--with-place-root (root &rest body)
  "Bind ROOT to a new root frame, and evaluate BODY.
The frame is on the session `remoteterm-tests--place-display', which
has a recording client.  Then delete all frames of the session, also
the frames that BODY made, and close the session."
  (declare (indent 1))
  `(remoteterm-tests--with-display remoteterm-tests--place-display
     (unwind-protect
         (let ((,root (remoteterm-tests--make-frame
                       remoteterm-tests--place-display
                       '((width . 60) (height . 30)))))
           (remote-display--record remoteterm-tests--place-display)
           ,@body)
       (dolist (frame (remoteterm-tests--place-frames))
         (when (frame-live-p frame)
           (delete-frame frame t))))))

(rd-daemon-deftest remoteterm-tests-child-frame-replay
  "The client gets the place of each frame (law C7).
After random operations on child frames, a replay of the output gives
each frame that the client knows the parent, the visibility and the
position that it has in Emacs.  The z values of the siblings have the
order of `frame--z-order-lessp'."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     (lambda () (rd-gen-child-frame-ops 12))
     (lambda (ops)
       (remoteterm-tests--with-place-root root
         (remoteterm-tests--run-child-ops root ops)
         (remoteterm-tests--check-replay))))))

(rd-daemon-deftest remoteterm-tests-child-frame-distinct-z
  "No two siblings have the same z order (law C8).
After each random operation, `frame--z-order-lessp' puts each frame
below or above each sibling.  This is a guard law.  It passed before
remoteplace.c existed, and it also passed with a raise or lower hook
that did not restack the siblings."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     (lambda () (rd-gen-child-frame-ops 12))
     (lambda (ops)
       (remoteterm-tests--with-place-root root
         (remoteterm-tests--run-child-ops
          root ops #'remoteterm-tests--check-distinct-z))))))

(rd-daemon-deftest remoteterm-tests-child-frame-place-before-define
  "A client gets the place of a new frame before its definition (law C9).
After an attach, the place that a client has for a frame at its first
define-frame is the place of the frame in Emacs.  The order in which
the client learns the frames does not matter."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     (lambda () (list (rd-gen-child-frame-ops 12) (= (rd-gen-random 2) 0)))
     (lambda (case)
       (remoteterm-tests--with-place-root root
         (remoteterm-tests--run-child-ops root (car case))
         (remoteterm-tests--reattach)
         (let ((frames (remoteterm-tests--place-frames)))
           (when (cadr case)
             (setq frames (reverse frames)))
           (mapc #'remoteterm-tests--show-frame frames)
           (remoteterm-tests--check-places
            frames (cdr (remoteterm-tests--replay-output)))))))))

(rd-daemon-deftest remoteterm-tests-frame-place-quiet-redisplay
  "A redisplay with no change of a place sends no place-frame (law C10).
This is a guard law.  It passed before remoteplace.c existed, and it
also passed when `remote_send_place' sent each place, also a place
that the client had."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     (lambda () (rd-gen-child-frame-ops 12))
     (lambda (ops)
       (remoteterm-tests--with-place-root root
         (remoteterm-tests--run-child-ops root ops)
         (mapc #'remoteterm-tests--show-frame (remoteterm-tests--place-frames))
         (remote-display--take-output remoteterm-tests--place-display)
         (redisplay t)
         (mapc #'remoteterm-tests--show-frame (remoteterm-tests--place-frames))
         (should-not (remoteterm-tests--places-in-output)))))))

(rd-daemon-deftest remoteterm-tests-frame-place-quiet-roots
  "A session with only visible root frames sends no place-frame (law C10).
Thus the output of such a session is the same as before child frames."
  (let ((rd-gen-trials 10))
    (rd-gen-for-all
     (lambda () (rd-gen-list (lambda () (list (rd-gen-integer -60 60)
                                              (rd-gen-integer -60 60)))
                             3))
     (lambda (positions)
       (remoteterm-tests--with-place-root _root
         (dolist (position positions)
           (let ((frame (remoteterm-tests--make-frame
                         remoteterm-tests--place-display nil)))
             (remoteterm-tests--show-frame frame)
             (apply #'set-frame-position frame position)
             (raise-frame frame)
             (lower-frame frame)
             (make-frame-visible frame)
             (remoteterm-tests--show-frame frame)))
         (redisplay t)
         (should-not (remoteterm-tests--places-in-output)))))))

(rd-daemon-deftest remoteterm-tests-child-frame-place-flush
  "A change of the place of a known child reaches the client (law C11).
A move, a raise or a hide of a child, with no redisplay, sends a
place-frame and flushes it."
  (remoteterm-tests--for-each-case
   '(move raise hide)
   (lambda (kind)
     (remoteterm-tests--with-place-root root
       (let ((low (remoteterm-tests--place-child root))
             (high (remoteterm-tests--place-child root)))
         (mapc #'remoteterm-tests--show-frame (list low high))
         (remote-display--take-output remoteterm-tests--place-display)
         (pcase kind
           ('move (set-frame-position low 7 9))
           ('raise (raise-frame low))
           ('hide (make-frame-invisible low t)))
         (should (= (remote-display--unflushed-bytes
                     remoteterm-tests--place-display)
                    0))
         (should (remoteterm-tests--places-in-output)))))))

(rd-daemon-deftest remoteterm-tests-child-frame-place-reconnect
  "A new client gets the same places as the last one (law C12).
After random operations, a detach and an attach, the replay of the
new output gives the places of the frames in Emacs."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     (lambda () (rd-gen-child-frame-ops 12))
     (lambda (ops)
       (remoteterm-tests--with-place-root root
         (remoteterm-tests--run-child-ops root ops)
         (remoteterm-tests--check-replay)
         (remoteterm-tests--reattach)
         (remoteterm-tests--check-replay))))))

(rd-daemon-deftest remoteterm-tests-child-frame-set-negative-position
  "`set-frame-position' with a negative value counts from the far edge.
With X = -N, the right edge of the child is N pixels from the right
edge of its parent, and `frame-position' gives a value that is not
negative.  Y is the same with the bottom edges (law C13)."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     (lambda () (list (rd-gen-integer -50 50) (rd-gen-integer -50 50)))
     (lambda (position)
       (remoteterm-tests--with-place-root root
         (let ((child (remoteterm-tests--place-child root)))
           (apply #'set-frame-position child position)
           (should (equal (frame-position child)
                          (cons (remoteterm-tests--edge
                                 (car position) (frame-native-width root)
                                 (frame-pixel-width child))
                                (remoteterm-tests--edge
                                 (cadr position) (frame-native-height root)
                                 (frame-pixel-height child)))))
           (should (>= (car (frame-position child)) 0))
           (should (>= (cdr (frame-position child)) 0))))))))

(defun remoteterm-tests--run-child-ops (root ops &optional check)
  "Apply OPS to ROOT and the children that OPS make.
OPS come from `rd-gen-child-frame-ops'.  Call CHECK, if not nil, with
the live frames of the session after each operation."
  (let ((frames (list root)))
    (dolist (op ops)
      (setq frames (seq-filter #'frame-live-p
                               (remoteterm-tests--apply-child-op frames op)))
      (when check
        (funcall check (remoteterm-tests--place-frames))))))

(defun remoteterm-tests--apply-child-op (frames op)
  "Apply the operation OP of `rd-gen-child-frame-ops' to FRAMES.
FRAMES is the list of frames that the indexes of OP name, with the root
frame first.  Return the new list.  Emacs refuses a parent that makes
a cycle, and then the operation changes nothing."
  (cl-flet ((frame (index) (nth (mod index (length frames)) frames)))
    (pcase op
      (`(make ,index ,left ,top ,visible)
       (setq frames
             (append frames
                     (list (remoteterm-tests--place-child
                            (frame index)
                            `((left . ,left) (top . ,top)
                              (visibility . ,visible)))))))
      (`(move ,index ,x ,y) (set-frame-position (frame index) x y))
      (`(visible ,index ,flag)
       (if flag
           (make-frame-visible (frame index))
         (make-frame-invisible (frame index) t)))
      (`(raise ,index) (raise-frame (frame index)))
      (`(lower ,index) (lower-frame (frame index)))
      (`(reparent ,index ,parent)
       (ignore-error error
         (set-frame-parameter (frame index) 'parent-frame
                              (and parent (frame parent)))))
      (`(delete ,index)
       (let ((target (frame index)))
         (unless (or (eq target (car frames))
                     (frame-ancestor-p target (car frames)))
           (delete-frame target t))))
      (`(show ,index) (remoteterm-tests--show-frame (frame index))))
    frames))

(defun remoteterm-tests--place-child (parent &optional parameters)
  "Make a small child frame of PARENT with PARAMETERS, and return it."
  (remoteterm-tests--make-child-frame
   remoteterm-tests--place-display
   (append parameters
           `((parent-frame . ,parent) (width . 10) (height . 4)))))

(defun remoteterm-tests--place-frames ()
  "Return the live frames of the session `remoteterm-tests--place-display'."
  (remoteterm-tests--session-frames remoteterm-tests--place-display))

(defun remoteterm-tests--show-frame (frame)
  "Make the client know FRAME: send a small draw on it."
  (remote-display--clear-area frame 0 0 1 1))

(defun remoteterm-tests--reattach ()
  "Detach the client of the place session, and attach a new one."
  (remote-display--detach remoteterm-tests--place-display)
  (remote-display--record remoteterm-tests--place-display))

(defun remoteterm-tests--check-replay ()
  "Show each frame of the place session, and check the places of its client."
  (let ((frames (remoteterm-tests--place-frames)))
    (mapc #'remoteterm-tests--show-frame frames)
    (remoteterm-tests--check-places
     frames (car (remoteterm-tests--replay-output)))))

(defun remoteterm-tests--places-in-output ()
  "Return the place-frame messages that the place session sent."
  (remoteterm-tests--of-type
   'place-frame (remote-display--take-output remoteterm-tests--place-display)))

(defun remoteterm-tests--replay-output ()
  "Take the output of the place session, and replay its places.
The value is the value of `remoteterm-tests--replay-places'."
  (remoteterm-tests--replay-places
   (remoteterm-tests--all-messages
    (remote-display--take-output remoteterm-tests--place-display))))

(defconst remoteterm-tests--default-place
  '(:parent 0 :x 0 :y 0 :z 0 :visible 1)
  "The place of a frame with no place-frame message.")

(defun remoteterm-tests--replay-places (messages)
  "Return the places of the frames that a client has after MESSAGES.
The value is (KNOWN . FIRST).  Each one is a hash table from a frame
ID to the fields of a place-frame message.  KNOWN has the place of each
frame that the client knows at the end.  FIRST has the place of each
frame at its first define-frame.  A place-frame for a frame that the
client does not know applies at the next define-frame of that frame.
A frame with no place-frame has `remoteterm-tests--default-place'."
  (let ((known (make-hash-table))
        (pending (make-hash-table))
        (first (make-hash-table)))
    (pcase-dolist (`(,type . ,fields) messages)
      (let ((id (plist-get fields :frame)))
        (pcase type
          ('place-frame
           (puthash id fields (if (gethash id known) known pending)))
          ('define-frame
           (unless (gethash id known)
             (let ((place (gethash id pending remoteterm-tests--default-place)))
               (puthash id place known)
               (unless (gethash id first)
                 (puthash id place first))
               (remhash id pending))))
          ('delete-frame
           (remhash id known)
           (remhash id pending)))))
    (cons known first)))

(defun remoteterm-tests--check-places (frames places)
  "Check that PLACES have the place of each frame of FRAMES in Emacs.
PLACES is a table of `remoteterm-tests--replay-places'."
  (dolist (frame frames)
    (let ((fields (gethash (remoteterm-tests--frame-id frame) places)))
      (should fields)
      (should (equal (remoteterm-tests--client-place fields)
                     (remoteterm-tests--emacs-place frame)))))
  (remoteterm-tests--do-siblings
   frames
   (lambda (a b)
     (should (eq (< (remoteterm-tests--place-z a places)
                    (remoteterm-tests--place-z b places))
                 (frame--z-order-lessp a b))))))

(defun remoteterm-tests--check-distinct-z (frames)
  "Check that no two siblings in FRAMES have the same z order."
  (remoteterm-tests--do-siblings
   frames
   (lambda (a b)
     (should (xor (frame--z-order-lessp a b) (frame--z-order-lessp b a))))))

(defun remoteterm-tests--do-siblings (frames function)
  "Call FUNCTION with each ordered pair of child frames in FRAMES.
The two frames of a pair have the same parent."
  (dolist (a frames)
    (dolist (b frames)
      (when (and (not (eq a b))
                 (frame-parent a)
                 (eq (frame-parent a) (frame-parent b)))
        (funcall function a b)))))

(defun remoteterm-tests--place-z (frame places)
  "Return the z value of FRAME in PLACES."
  (plist-get (gethash (remoteterm-tests--frame-id frame) places) :z))

(defun remoteterm-tests--client-place (fields)
  "Return the part of the place FIELDS that a client uses.
The value is (PARENT VISIBLE X Y).  X and Y are nil for a frame with
no parent, because a client does not use them."
  (let ((child (/= (plist-get fields :parent) 0)))
    (list (plist-get fields :parent)
          (plist-get fields :visible)
          (and child (plist-get fields :x))
          (and child (plist-get fields :y)))))

(defun remoteterm-tests--emacs-place (frame)
  "Return the place of FRAME in Emacs, as `remoteterm-tests--client-place'."
  (let ((parent (frame-parent frame)))
    (list (if parent (remoteterm-tests--frame-id parent) 0)
          (if (eq (frame-visible-p frame) t) 1 0)
          (and parent (car (frame-position frame)))
          (and parent (cdr (frame-position frame))))))

;;;; Colors

(defun remoteterm-tests--rgb ()
  "Return a random list of three 8-bit components."
  (list (rd-gen-integer 0 255) (rd-gen-integer 0 255)
        (rd-gen-integer 0 255)))

(ert-deftest remoteterm-tests-hex-color-values ()
  (skip-unless (featurep 'remote-test))
  (rd-gen-for-all
   #'remoteterm-tests--rgb
   (lambda (rgb)
     (should (equal (xw-color-values (apply #'format "#%02x%02x%02X" rgb))
                    (mapcar (lambda (c) (* c 257)) rgb))))))

(defun remoteterm-tests--color-string ()
  "Return a random string that is often, but not always, a color."
  (rd-gen-element
   (list (apply #'format "#%02x%02x%02x" (remoteterm-tests--rgb))
         (apply #'format "rgb:%x/%x/%x" (remoteterm-tests--rgb))
         (rd-gen-element '("red" "Light Blue" "LightBlue" "no such color"))
         (concat "#" (mapconcat #'number-to-string
                                (rd-gen-list (lambda () (rd-gen-random 10))
                                             8)))
         (string-to-unibyte (rd-gen-bytes 6)))))

(ert-deftest remoteterm-tests-color-defined-iff-values ()
  (skip-unless (featurep 'remote-test))
  (rd-gen-for-all
   #'remoteterm-tests--color-string
   (lambda (string)
     (should (eq (and (xw-color-defined-p string) t)
                 (and (xw-color-values string) t))))))

(ert-deftest remoteterm-tests-named-color-before-session ()
  "Named colors work before any session is open."
  (skip-unless (featurep 'remote-test))
  (let ((output
         (with-output-to-string
           (call-process (expand-file-name invocation-name
                                           invocation-directory)
                         nil standard-output nil
                         "-Q" "--batch" "--eval"
                         "(princ (xw-color-values \"red\"))"))))
    (should (equal output "(65535 0 0)"))))

;;;; Drawing

(defun remoteterm-tests--messages (bytes)
  "Return the list of messages in BYTES, without the frame messages.
The drawing tests check draws, so this list does not have the
define-frame and delete-frame messages.  The tests of the frame
messages use `remoteterm-tests--all-messages'."
  (seq-remove (lambda (message)
                (memq (car message) '(define-frame delete-frame)))
              (remoteterm-tests--all-messages bytes)))

(defun remoteterm-tests--all-messages (bytes)
  "Return the list of messages in the unibyte string BYTES."
  (let ((messages nil)
        (start 0))
    (while (< start (length bytes))
      (let ((result (remote-display--decode bytes start)))
        (push (car result) messages)
        (setq start (+ start (cdr result)))))
    (nreverse messages)))

(defun remoteterm-tests--glyph-runs (bytes)
  "Return the glyph vectors of the draw-glyphs messages in BYTES."
  (mapcar (lambda (message) (plist-get (cdr message) :glyphs))
          (seq-filter (lambda (message) (eq (car message) 'draw-glyphs))
                      (remoteterm-tests--messages bytes))))

(defun remoteterm-tests--draw-text-form (text)
  "Return a form for the daemon that draws TEXT on a new remote frame.
The value of the form is a plist.  :output is the bytes that the
session sent.  :codes is the vector of the glyph codes of the frame
font for TEXT.  :geometry is as for `remoteterm-tests--geometry-form'.
The frame is wide enough for TEXT on one line."
  (remoteterm-tests--frame-form
   `((width . ,(+ (length text) 20)))
   `(insert ,text)
   `(list :output output
          :codes ,(remoteterm-tests--codes-form text)
          :geometry ,(remoteterm-tests--geometry-form))))

(defun remoteterm-tests--drawn-p (codes bytes)
  "Return non-nil if the glyph runs in BYTES draw CODES in sequence.
A run can continue after the text, for example with the space glyph
at the end of a buffer, and a long text can need more than one run."
  (cl-search codes (apply #'vconcat (remoteterm-tests--glyph-runs bytes))))

(defvar remote-display--max-glyphs)
(defvar remote-display--max-fringe-rows)

(defun remoteterm-tests--split-lengths ()
  "Return text lengths at the limits of the glyphs of one draw-glyphs message."
  (let ((limit remote-display--max-glyphs))
    (list 1 (1- limit) limit (1+ limit) (* 2 limit) (1+ (* 2 limit)))))

(defun remoteterm-tests--text-length (splits)
  "Return a random text length for a text with up to SPLITS splits.
The maximum is one glyph more than SPLITS full messages, so the
length follows the limit.  The length is often at a split limit."
  (let ((max (1+ (* splits remote-display--max-glyphs))))
    (if (zerop (rd-gen-random 2))
        (rd-gen-element (seq-filter (lambda (n) (<= n max))
                                    (remoteterm-tests--split-lengths)))
      (rd-gen-integer 1 max))))

(defun remoteterm-tests--text ()
  "Return a random line of printable ASCII text."
  (apply #'string
         (cl-loop repeat (remoteterm-tests--text-length 1)
                  collect (rd-gen-integer ?\s ?~))))

(rd-daemon-deftest remoteterm-tests-draw-text
  "A frame draws a text with the glyph codes of its font."
  (let ((rd-gen-trials 50))
    (rd-gen-for-all
     #'remoteterm-tests--text
     (lambda (text)
       (pcase-let (((map :output :codes)
                    (rd-daemon-eval
                     (remoteterm-tests--draw-text-form text))))
         (should (remoteterm-tests--drawn-p codes output)))))))

(defun remoteterm-tests--line-runs (bytes y)
  "Return the draw-glyphs messages of text in BYTES on the baseline Y."
  (seq-filter (lambda (message)
                (and (eq (car message) 'draw-glyphs)
                     (= (plist-get (cdr message) :y) y)))
              (remoteterm-tests--text-messages bytes)))

(defun remoteterm-tests--text-messages (bytes)
  "Return the messages in BYTES without the glyphs on a box cursor.
After a box, the server draws the glyph under the box again."
  (let ((messages nil)
        (on-box nil))
    (dolist (message (remoteterm-tests--messages bytes) (nreverse messages))
      (pcase (car message)
        ('draw-cursor (setq on-box (= (plist-get (cdr message) :kind) 0)))
        ('draw-glyphs nil)
        (_ (setq on-box nil)))
      (unless (and on-box (eq (car message) 'draw-glyphs))
        (push message messages)))))

(rd-daemon-deftest remoteterm-tests-draw-position
  "Each run of glyphs of a line starts where the run before it ends."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     (lambda () (make-string (remoteterm-tests--text-length 2) ?x))
     (lambda (text)
       (pcase-let* (((map :output
                          (:geometry (map :x0 :top :ascent :char-width)))
                     (rd-daemon-eval
                      (remoteterm-tests--draw-text-form text)))
                    (runs (remoteterm-tests--line-runs output
                                                       (+ top ascent))))
         (should runs)
         (should (= (plist-get (cdr (car runs)) :x) x0))
         (cl-loop for (run next) on runs while next
                  do (should (= (plist-get (cdr next) :x)
                                (+ (plist-get (cdr run) :x)
                                   (* char-width
                                      (length (plist-get (cdr run)
                                                         :glyphs))))))))))))

(defun remoteterm-tests--frame-of-text-form (texts)
  "Return a form for the daemon that draws each of TEXTS on a new frame.
The value of the form is a plist.  :output is the bytes that the
session sent, and :codes is a list of the glyph codes of each text."
  `(let* ((display "remote-draw")
          (frames (progn
                    (x-open-connection display)
                    (mapcar (lambda (_)
                              (x-create-frame-with-faces
                               '((display . "remote-draw"))))
                            ',texts))))
     (unwind-protect
         (progn
           (cl-mapc (lambda (frame text)
                      (let ((buffer (generate-new-buffer "draw")))
                        (with-current-buffer buffer (insert text))
                        (set-window-buffer (frame-root-window frame)
                                           buffer)))
                    frames ',texts)
           (select-frame (car frames))
           (remote-display--record display)
           (redisplay t)
           (list :output (remote-display--take-output display)
                 :codes
                 (cl-mapcar (lambda (frame text)
                              (let ((window (frame-root-window frame)))
                                (with-current-buffer (window-buffer window)
                                  (vconcat
                                   (mapcar (lambda (glyph) (aref glyph 3))
                                           (font-get-glyphs
                                            (font-at 1 window)
                                            0 (length text) text))))))
                            frames ',texts)))
       (mapc #'delete-frame frames))))

(defun remoteterm-tests--frame-ids (codes bytes)
  "Return the frame IDs of the glyph runs in BYTES that start with CODES."
  (delete-dups
   (mapcar (lambda (message) (plist-get (cdr message) :frame))
           (seq-filter (lambda (message)
                         (and (eq (car message) 'draw-glyphs)
                              (equal (seq-take (plist-get (cdr message)
                                                          :glyphs)
                                               (length codes))
                                     codes)))
                       (remoteterm-tests--messages bytes)))))

(defun remoteterm-tests--distinct-texts (count)
  "Return COUNT texts that start with different characters."
  (cl-loop for i below count
           collect (make-string 4 (+ ?a i))))

(rd-daemon-deftest remoteterm-tests-frame-ids
  "The frames of a session draw with distinct frame IDs, one per frame."
  (let ((rd-gen-trials 10))
    (rd-gen-for-all
     (lambda () (remoteterm-tests--distinct-texts (rd-gen-integer 2 6)))
     (lambda (texts)
       (pcase-let* (((map :output :codes)
                     (rd-daemon-eval
                      (remoteterm-tests--frame-of-text-form texts)))
                    (ids (mapcar (lambda (c)
                                   (remoteterm-tests--frame-ids c output))
                                 codes)))
         (should (cl-every (lambda (frame-ids) (= (length frame-ids) 1))
                           ids))
         (should (= (length (delete-dups (apply #'append ids)))
                    (length texts))))))))

(ert-deftest remoteterm-tests-no-client ()
  "With no client, redisplay skips the frame and a session has no sink.
The empty cursor state shows that redisplay did not reach the hooks.  A
client that comes later gets the text when a redraw sends it."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let (((map :cursor :no-client :output)
                 (rd-daemon-eval
                  '(let* ((display "remote-draw")
                          (frame (progn (x-open-connection display)
                                        (x-create-frame-with-faces
                                         '((display . "remote-draw")))))
                          (buffer (generate-new-buffer "draw")))
                     (select-frame frame)
                     (set-window-buffer (selected-window) buffer)
                     (remote-display--focus frame)
                     (with-current-buffer buffer (insert "abc"))
                     (redraw-frame frame)
                     (redisplay t)
                     (prog1
                         (list :cursor (window-cursor-info)
                               :no-client (condition-case nil
                                              (remote-display--take-output
                                               display)
                                            (error 'no-client))
                               :output (progn
                                         (remote-display--record display)
                                         (redraw-frame frame)
                                         (redisplay t)
                                         (remote-display--take-output display)))
                       (delete-frame frame)
                       (kill-buffer buffer))))))
      (should-not cursor)
      (should (eq no-client 'no-client))
      (should (assq 'draw-glyphs (remoteterm-tests--messages output))))))

(defun remoteterm-tests--codes-form (text)
  "Return a form that gives the vector of the glyph codes of TEXT.
The form uses the variable FONT."
  `(vconcat (mapcar (lambda (glyph) (aref glyph 3))
                    (font-get-glyphs font 0 ,(length text) ,text))))

(defun remoteterm-tests--geometry-form ()
  "Return a form that gives the geometry of the text of WINDOW.
The form uses the variables FRAME, WINDOW and FONT.  Its value is a
plist.  :x0 and :top are the pixel position of the left edge and of
the top of the text area, :ascent is the ascent of FONT, :char-width
is the width of a character, and :line-height is the height of a
line."
  '(let ((edges (window-inside-pixel-edges window)))
     (list :x0 (nth 0 edges)
           :top (nth 1 edges)
           :ascent (aref (query-font font) 4)
           :char-width (frame-char-width frame)
           :line-height (frame-char-height frame))))

(defun remoteterm-tests--frame-form (parameters setup result)
  "Return a form for the daemon that draws a buffer on a new remote frame.
This is the setup of each drawing test.  PARAMETERS is the list of the
parameters of the frame.  The frame has no bars unless PARAMETERS ask
for them (see `remoteterm-tests--plain-layout').  The form evaluates
the form SETUP with the buffer current.  SETUP must insert text, and
can use the variables FRAME and WINDOW.  The value of the form is the
value of the form RESULT.  RESULT can use the variable OUTPUT, the
bytes that the session sent for the first draw, the variables DISPLAY
and BUFFER, and the variables of `remoteterm-tests--geometry-form'.
FONT is the font of the last character that is not a newline."
  `(let* ((display "remote-draw")
          (frame (progn (x-open-connection display)
                        (x-create-frame-with-faces
                         '((display . "remote-draw")
                           ,@parameters
                           ,@remoteterm-tests--plain-layout))))
          (window (frame-root-window frame))
          (buffer (generate-new-buffer "draw")))
     (unwind-protect
         (progn
           (set-window-buffer window buffer)
           (select-frame frame)
           (remote-display--focus frame)
           (with-current-buffer buffer ,setup)
           (remote-display--record display)
           (redisplay t)
           (let ((output (remote-display--take-output display))
                 (font (with-current-buffer buffer
                         (save-excursion
                           (goto-char (point-max))
                           (skip-chars-backward "\n")
                           (font-at (1- (point)) window)))))
             ,result))
       (delete-frame frame)
       (kill-buffer buffer))))

(defun remoteterm-tests--shorten-form (old new)
  "Return a form for the daemon that draws OLD, and then NEW in its place.
The value of the form is as for `remoteterm-tests--replace-form', with
more properties.  :codes is the vector of the glyph codes of NEW, and
:space is the glyph code of a space."
  (remoteterm-tests--replace-form
   old new
   :codes (remoteterm-tests--codes-form new)
   :space `(aref ,(remoteterm-tests--codes-form " ") 0)))

(defun remoteterm-tests--replay (messages rows columns geometry)
  "Return the screen after MESSAGES, replayed in order.
The screen is a vector of ROWS rows of the text area, and each row is
a vector of COLUMNS cells.  GEOMETRY is as for
`remoteterm-tests--geometry-form'.  The state of a cell is nil if no
message changed it, `clear' if a clear-area message covered it,
`cursor' if a draw-cursor message covered it, or the code of the glyph
that a draw-glyphs message drew on it.  A glyph on a cursor cell does
not change it, because the server draws the glyph under a box again
and the box stays."
  (let ((screen (apply #'vector
                       (cl-loop repeat rows
                                collect (make-vector columns nil)))))
    (dolist (message messages screen)
      (let ((fields (cdr message)))
        (pcase (car message)
          ('draw-glyphs
           (remoteterm-tests--replay-draw screen fields geometry))
          ('clear-area
           (remoteterm-tests--replay-clear screen fields geometry))
          ('copy-area
           (remoteterm-tests--replay-copy screen fields geometry))
          ('draw-cursor
           (remoteterm-tests--replay-cursor screen fields geometry))
          ('draw-fringe nil)
          ('define-font nil)
          (_ (error "The model has no message type %s" (car message))))))))

(defun remoteterm-tests--replay-draw (screen fields geometry)
  "Put the glyphs of the draw-glyphs FIELDS into the cells of SCREEN."
  (pcase-let* (((map :x0 :top :ascent :char-width :line-height) geometry)
               (row (remoteterm-tests--index
                     (- (plist-get fields :y) ascent top) line-height))
               (column (remoteterm-tests--index (- (plist-get fields :x) x0)
                                                char-width)))
    (when (and row column)
      (seq-do-indexed
       (lambda (glyph i)
         (unless (eq (remoteterm-tests--cell screen row (+ column i)) 'cursor)
           (remoteterm-tests--set-cell screen row (+ column i) glyph)))
       (plist-get fields :glyphs)))))

(defun remoteterm-tests--cell (screen row column)
  "Return the cell of SCREEN at ROW and COLUMN, or nil if it does not exist."
  (and (< -1 row (length screen))
       (< -1 column (length (aref screen row)))
       (aref (aref screen row) column)))

(defun remoteterm-tests--replay-clear (screen fields geometry)
  "Clear the cells of SCREEN inside the rectangle of the clear-area FIELDS."
  (remoteterm-tests--do-cells
   screen fields geometry
   (lambda (row column)
     (remoteterm-tests--set-cell screen row column 'clear))))

(defun remoteterm-tests--replay-cursor (screen fields geometry)
  "Mark the cells of SCREEN inside the rectangle of the draw-cursor FIELDS."
  (remoteterm-tests--do-cells
   screen fields geometry
   (lambda (row column)
     (remoteterm-tests--set-cell screen row column 'cursor))))

(defun remoteterm-tests--replay-copy (screen fields geometry)
  "Copy the cells of SCREEN inside the rectangle of the copy-area FIELDS.
Copy them by the distance from :x, :y to :to-x, :to-y."
  (pcase-let* (((map :char-width :line-height) geometry)
               (rows (remoteterm-tests--index
                      (- (plist-get fields :to-y) (plist-get fields :y))
                      line-height))
               (columns (remoteterm-tests--index
                         (- (plist-get fields :to-x) (plist-get fields :x))
                         char-width))
               (source (apply #'vector
                              (mapcar #'copy-sequence screen))))
    (unless (and rows columns)
      (error "The copy distance is not a number of cells: %S" fields))
    (remoteterm-tests--do-cells
     source fields geometry
     (lambda (row column)
       (remoteterm-tests--set-cell screen (+ row rows) (+ column columns)
                                   (aref (aref source row) column))))))

(defun remoteterm-tests--do-cells (screen fields geometry function)
  "Call FUNCTION with the row and column of each cell of SCREEN.
Call it only for the cells inside the rectangle of FIELDS, which has
the properties :x, :y, :width and :height."
  (pcase-let (((map :x0 :top :char-width :line-height) geometry)
              (x (plist-get fields :x))
              (y (plist-get fields :y)))
    (dotimes (row (length screen))
      (dotimes (column (length (aref screen row)))
        (let ((left (+ x0 (* column char-width)))
              (row-top (+ top (* row line-height))))
          (when (and (<= x left)
                     (<= (+ left char-width) (+ x (plist-get fields :width)))
                     (<= y row-top)
                     (<= (+ row-top line-height)
                         (+ y (plist-get fields :height))))
            (funcall function row column)))))))

(defun remoteterm-tests--index (distance size)
  "Return DISTANCE divided by SIZE, or nil if SIZE does not divide it."
  (and (zerop (mod distance size)) (/ distance size)))

(defun remoteterm-tests--set-cell (screen row column state)
  "Set the cell of SCREEN at ROW and COLUMN to STATE, if it exists."
  (when (and (< -1 row (length screen))
             (< -1 column (length (aref screen row))))
    (aset (aref screen row) column state)))

(rd-daemon-deftest remoteterm-tests-clear
  "After a line becomes shorter, its last columns are clear."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     (lambda ()
       (let ((old (rd-gen-integer 1 100)))
         (cons (make-string old ?x)
               (make-string (rd-gen-integer 0 (1- old)) ?y))))
     (lambda (texts)
       (pcase-let* ((`(,old . ,new) texts)
                    ((map :output :codes :space :geometry)
                     (rd-daemon-eval
                      (remoteterm-tests--shorten-form old new)))
                    (row (aref (remoteterm-tests--replay
                                (remoteterm-tests--messages output)
                                1 (length old) geometry)
                               0)))
         (should (equal (seq-take row (length new)) codes))
         (should (cl-every (lambda (state)
                             (memq state (list 'clear 'cursor space)))
                           (seq-drop row (length new)))))))))

(defconst remoteterm-tests--scroll-lines 60
  "The number of lines of the buffer of the scroll test.")

(defun remoteterm-tests--scroll-form (height extra from to)
  "Return a form for the daemon that scrolls a window of a new frame.
The frame has HEIGHT lines and EXTRA pixels, so that the last line can
be visible only in part.  The window starts at line FROM of the buffer
of `remoteterm-tests--fill-lines-form', and then at line TO.  The value
of the form is as for `remoteterm-tests--scroll-result-form'."
  (remoteterm-tests--frame-form
   `((width . 20) (height . ,height))
   `(progn
      ,(remoteterm-tests--fill-lines-form)
      ,(remoteterm-tests--grow-frame-form extra)
      ,(remoteterm-tests--window-start-form from))
   `(progn
      ,(remoteterm-tests--window-start-form to)
      (redisplay t)
      ,(remoteterm-tests--scroll-result-form))))

(defun remoteterm-tests--fill-lines-form ()
  "Return a form that inserts lines of 3 digits, the numbers from 0."
  `(dotimes (i ,remoteterm-tests--scroll-lines)
     (insert (format "%03d\n" i))))

(defun remoteterm-tests--grow-frame-form (pixels)
  "Return a form that adds PIXELS to the height of the text of FRAME."
  `(let ((frame-resize-pixelwise t))
     (set-frame-size frame (frame-text-width frame)
                     (+ (frame-text-height frame) ,pixels) t)))

(defun remoteterm-tests--window-start-form (line)
  "Return a form that starts WINDOW at LINE, with point at its start."
  `(with-current-buffer buffer
     (goto-char (point-min))
     (forward-line ,line)
     (set-window-start window (point))
     (set-window-point window (point))))

(defun remoteterm-tests--scroll-result-form ()
  "Return a form that gives the values that the scroll tests compare.
The value of the form is a plist.  :output is the bytes that the
session sent for both window starts.  :rows is the number of lines of
the text area.  :bottom is the pixel position of the bottom of the text
area.  :codes is a list of the glyph codes of each line.  :geometry is
as for `remoteterm-tests--geometry-form'."
  `(list :output (concat output (remote-display--take-output display))
         :rows (window-body-height window)
         :bottom (nth 3 (window-inside-pixel-edges window))
         :codes (with-current-buffer buffer
                  (mapcar (lambda (line)
                            (vconcat (mapcar (lambda (glyph) (aref glyph 3))
                                             (font-get-glyphs font 0 3 line))))
                          (split-string (buffer-string) "\n" t)))
         :geometry ,(remoteterm-tests--geometry-form)))

(defun remoteterm-tests--scroll-case ()
  "Return random arguments for `remoteterm-tests--scroll-form'."
  (list (rd-gen-integer 4 12) (rd-gen-integer 0 20)
        (rd-gen-integer 0 20) (rd-gen-integer 0 20)))

(rd-daemon-deftest remoteterm-tests-scroll
  "After a change of the window start, the screen shows the new lines."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     #'remoteterm-tests--scroll-case
     (lambda (case)
       (pcase-let* ((`(,_ ,_ ,_ ,to) case)
                    ((map :output :rows :codes :geometry)
                     (rd-daemon-eval
                      (apply #'remoteterm-tests--scroll-form case)))
                    (screen (remoteterm-tests--replay
                             (remoteterm-tests--messages output)
                             rows 3 geometry)))
         ;; Point is at the start of the window, and only there.
         (let ((expected (mapcar #'copy-sequence
                                 (seq-subseq codes to (+ to rows)))))
           (aset (car expected) 0 'cursor)
           (should (equal (append screen nil) expected))))))))

(defun remoteterm-tests--copy-inside-p (fields before after)
  "Return non-nil if the copy-area FIELDS obey law 9 for one window.
BEFORE and AFTER are the text area of the window before and after the
redisplay, each as (TOP . BOTTOM).  Redisplay draws a mode line before
it copies and a header line after it copies, so the bottom of the
source must also be in AFTER."
  (pcase-let* (((map :y :to-y :height) fields)
               (`(,top . ,bottom) before)
               (`(,to-top . ,to-bottom) after))
    (and (<= top y (+ y height) (min bottom to-bottom))
         (<= to-top to-y (+ to-y height) to-bottom))))

(defun remoteterm-tests--copies (messages)
  "Return the fields of the copy-area messages in MESSAGES."
  (mapcar #'cdr (seq-filter (lambda (message) (eq (car message) 'copy-area))
                            messages)))

(defun remoteterm-tests--copies-inside-p (messages before after)
  "Return non-nil if each copy-area in MESSAGES obeys law 9 for one window.
BEFORE and AFTER are lists with the text area of each window before and
after the redisplay that sent MESSAGES."
  (cl-every (lambda (fields)
              (cl-some (lambda (areas)
                         (remoteterm-tests--copy-inside-p fields (car areas)
                                                          (cdr areas)))
                       (cl-mapcar #'cons before after)))
            (remoteterm-tests--copies messages)))

(rd-daemon-deftest remoteterm-tests-scroll-bounds
  "A copy stays in the text area, and a short scroll sends a copy."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     #'remoteterm-tests--scroll-case
     (lambda (case)
       (pcase-let* ((`(,_ ,_ ,from ,to) case)
                    ((map :output :rows :bottom (:geometry (map :top)))
                     (rd-daemon-eval
                      (apply #'remoteterm-tests--scroll-form case)))
                    (messages (remoteterm-tests--messages output)))
         (let ((areas (list (cons top bottom))))
           (should (remoteterm-tests--copies-inside-p messages areas areas)))
         (when (< 0 (abs (- to from)) rows)
           (should (assq 'copy-area messages))))))))

(defun remoteterm-tests--scroll-layouts ()
  "Return the layouts of a window that scrolls, as (PARAMETERS . FORMS).
PARAMETERS is the list of the parameters of the frame.  FORMS change
the buffer, or make WINDOW a different window."
  (let ((tall '(save-excursion
                 (goto-char (point-min))
                 (while (not (eobp))
                   (put-text-property (point) (line-beginning-position 2)
                                      'face '(:height 4.0))
                   (forward-line 2))))
        (tall-first '(progn
                       (put-text-property 1 5 'face '(:height 5.0))
                       (setq-local make-cursor-line-fully-visible nil)))
        (lower-window '((setq window (split-window window 10))
                        (set-window-buffer window buffer))))
    `((((height . 12)))
      (((height . 12)) ,(remoteterm-tests--grow-frame-form 7))
      (((height . 12)) (setq header-line-format "H"))
      (((height . 12)) (setq tab-line-format "T"))
      (((height . 12)) (setq header-line-format "H" tab-line-format "T")
       ,(remoteterm-tests--grow-frame-form 7))
      (((height . 12)) (setq line-spacing 5)
       ,(remoteterm-tests--grow-frame-form 3))
      (((height . 12)) (setq mode-line-format nil)
       ,(remoteterm-tests--grow-frame-form 7))
      (((height . 16)) ,tall)
      (((height . 16)) ,tall ,(remoteterm-tests--grow-frame-form 9))
      (((height . 16)) ,tall (setq header-line-format "H"))
      (((height . 16)) ,tall (setq-local make-cursor-line-fully-visible nil))
      (((height . 30)) ,@lower-window)
      (((height . 30)) ,tall ,@lower-window)
      (((height . 30)) (setq header-line-format "H") ,@lower-window)
      (((height . 30) (menu-bar-lines . 1)) ,tall ,@lower-window)
      (((height . 30)) (setq line-spacing 7) ,@lower-window)
      (((height . 16)) ,tall-first (setq header-line-format "H"))
      (((height . 30)) ,tall-first ,@lower-window)
      (((height . 30)) ,tall-first (setq header-line-format "H")
       ,@lower-window))))

(defun remoteterm-tests--delete-line-form (line)
  "Return a form that deletes line LINE of the buffer."
  `(with-current-buffer buffer
     (goto-char (point-min))
     (forward-line ,line)
     (delete-region (point) (line-beginning-position 2))))

(defun remoteterm-tests--insert-line-form (line)
  "Return a form that adds a new line before line LINE of the buffer."
  `(with-current-buffer buffer
     (goto-char (point-min))
     (forward-line ,line)
     (insert "new\n")))

(defun remoteterm-tests--move-line-form (from to)
  "Return a form that moves line FROM of the buffer to line TO."
  `(with-current-buffer buffer
     (goto-char (point-min))
     (forward-line ,from)
     (let ((line (delete-and-extract-region (point)
                                            (line-beginning-position 2))))
       (goto-char (point-min))
       (forward-line ,to)
       (insert line))))

(defconst remoteterm-tests--toggle-header-line
  '(with-current-buffer buffer
     (setq header-line-format (if header-line-format nil "H")))
  "A form that adds a header line, or removes it.")

(defconst remoteterm-tests--toggle-mode-line
  '(with-current-buffer buffer
     (setq mode-line-format (if mode-line-format nil "M")))
  "A form that adds a mode line, or removes it.")

(defun remoteterm-tests--vscroll-form (pixels)
  "Return a form that gives WINDOW a vertical scroll of PIXELS."
  `(set-window-vscroll window ,pixels t))

(defun remoteterm-tests--scroll-changes ()
  "Return lists of forms, each a sequence of changes of a window.
Redisplay runs after each form.  The sequences reach each caller of
the scroll hook: a new window start, a change in the text, and rows that
change their order."
  (let ((vscroll #'remoteterm-tests--vscroll-form)
        (header remoteterm-tests--toggle-header-line)
        (mode-line remoteterm-tests--toggle-mode-line)
        (start #'remoteterm-tests--window-start-form)
        (delete #'remoteterm-tests--delete-line-form)
        (insert #'remoteterm-tests--insert-line-form)
        (move #'remoteterm-tests--move-line-form))
    (list
     ;; New window starts, near and far.
     (mapcar start '(5 8 6 2))
     (mapcar start '(40 3 41))
     ;; A change of the text.
     (list (funcall delete 2) (funcall delete 4) (funcall delete 0))
     (list (funcall insert 2) (funcall insert 4) (funcall insert 0))
     (list (funcall move 1 6) (funcall move 6 1) (funcall move 0 3))
     ;; A first line that is visible in part.
     (list (funcall vscroll 5) (funcall vscroll 3) (funcall vscroll 9))
     (list (funcall vscroll 5) (funcall delete 1) (funcall insert 2))
     (list (funcall vscroll 5) (funcall move 1 4) (funcall start 2))
     ;; A row visible in part that keeps its visible height, but not
     ;; its position, when the top of the text area moves.
     (list (funcall vscroll 23) header header)
     (list (funcall vscroll 6) header (funcall vscroll 3) header)
     (list (funcall vscroll 23) mode-line mode-line)
     ;; A change of the height of the text area and of the rows.
     (list '(with-current-buffer buffer
              (setq header-line-format
                    (propertize "H" 'face '(:height 3.0))))
           (funcall delete 1))
     (list '(with-current-buffer buffer (setq mode-line-format nil))
           (funcall delete 1))
     (list '(with-current-buffer buffer (text-scale-set 2))
           (funcall delete 1))
     (list '(when (window-parent window) (window-resize window 2))
           (funcall delete 1))
     ;; Scroll commands, by more lines than the window has.
     (list '(with-selected-window window (scroll-up 15))
           '(with-selected-window window (scroll-down 14))
           '(with-selected-window window (scroll-up 2))))))

(defun remoteterm-tests--scroll-layout-form (layout changes direct)
  "Return a form for the daemon that makes CHANGES in a window of LAYOUT.
LAYOUT is an element of `remoteterm-tests--scroll-layouts', and CHANGES
an element of `remoteterm-tests--scroll-changes'.  If DIRECT is nil,
redisplay does not reuse rows of the current matrix, so that the update
of the window compares the rows.  The value of the form is a list with
one plist for each change.  :output is the bytes that the session sent
for the change.  :before and :after are lists of (TOP . BOTTOM) for the
text area of each window before and after the change."
  (remoteterm-tests--frame-form
   (car layout)
   `(progn
      ,(remoteterm-tests--fill-lines-form)
      (setq inhibit-try-window-id ,(not direct)
            inhibit-try-window-reusing ,(not direct))
      ,@(cdr layout)
      ,(remoteterm-tests--window-start-form 0))
   `(let ((areas (lambda ()
                   (mapcar (lambda (window)
                             (let ((edges (window-inside-pixel-edges window)))
                               (cons (nth 1 edges) (nth 3 edges))))
                           (window-list frame))))
          (steps nil))
      ,@(mapcar (lambda (change)
                  `(let ((before (funcall areas)))
                     ,change
                     (redisplay t)
                     (push (list :output (remote-display--take-output display)
                                 :before before
                                 :after (funcall areas))
                           steps)))
                changes)
      (nreverse steps))))

(defun remoteterm-tests--check-scroll-steps (steps)
  "Check law 9 for each of STEPS, and return the number of copies.
STEPS is a value of the form of `remoteterm-tests--scroll-layout-form'."
  (let ((copies 0))
    (pcase-dolist ((map :output :before :after) steps)
      (let ((messages (remoteterm-tests--messages output)))
        (cl-incf copies (length (remoteterm-tests--copies messages)))
        (should (remoteterm-tests--copies-inside-p messages before after))))
    copies))

(rd-daemon-deftest remoteterm-tests-scroll-bounds-layouts
  "A copy stays in the text area of a window, for each kind of window.
The windows have a header line, a tab line, a window above them, line
spacing, tall lines, or a line visible in part, and they scroll in each
way that redisplay can."
  (let ((rd-gen-trials 60)
        (copies 0))
    (rd-gen-for-all
     (lambda ()
       (list (rd-gen-element (remoteterm-tests--scroll-layouts))
             (rd-gen-element (remoteterm-tests--scroll-changes))
             (rd-gen-element '(nil t))))
     (lambda (case)
       (cl-incf copies
                (remoteterm-tests--check-scroll-steps
                 (rd-daemon-eval
                  (apply #'remoteterm-tests--scroll-layout-form case))))))
    (should (> copies 0))))

(defun remoteterm-tests--header-toggle-steps (height vscroll changes)
  "Return the steps of CHANGES in the lower of two windows with a header line.
The first line of the buffer has the face height HEIGHT, and the window
has a vertical scroll of VSCROLL pixels.  CHANGES is a list of forms.
The value is as for `remoteterm-tests--scroll-layout-form'."
  (rd-daemon-eval
   (remoteterm-tests--scroll-layout-form
    `(((height . 30))
      (put-text-property 1 5 'face '(:height ,height))
      (setq-local make-cursor-line-fully-visible nil)
      (setq header-line-format "H")
      (setq window (split-window window 10))
      (set-window-buffer window buffer))
    (cons (remoteterm-tests--vscroll-form vscroll) changes)
    t)))

(defun remoteterm-tests--lower-window-copy (step)
  "Return the fields of the copy of the lower window in STEP, and its areas.
The value is (FIELDS BEFORE AFTER), where BEFORE and AFTER are the text
area of the lower window before and after STEP."
  (pcase-let* (((map :output :before :after) step)
               (`(,_ ,lower-before) before)
               (`(,_ ,lower-after) after))
    (list (seq-find (lambda (fields)
                      (>= (plist-get fields :to-y) (car lower-after)))
                    (remoteterm-tests--copies
                     (remoteterm-tests--messages output)))
          lower-before lower-after)))

(ert-deftest remoteterm-tests-scroll-header-line-goes ()
  "A row cut at the top moves up when a header line goes away.
The row starts above the text area, and the copy must not reach the
window above."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (dolist (vscroll '(6 23))
      (let ((steps (remoteterm-tests--header-toggle-steps
                    5.0 vscroll (list remoteterm-tests--toggle-header-line))))
        (remoteterm-tests--check-scroll-steps steps)
        (pcase-let ((`(,copy (,old-top . ,_) (,new-top . ,bottom))
                     (remoteterm-tests--lower-window-copy (car (last steps)))))
          (ert-info ((format "%S" vscroll) :prefix "Vertical scroll: ")
            (should (> old-top new-top))
            (should (equal (plist-get copy :y) old-top))
            (should (equal (plist-get copy :to-y) new-top))
            (should (<= (+ new-top (plist-get copy :height)) bottom))))))))

(ert-deftest remoteterm-tests-scroll-header-line-comes ()
  "A copy keeps its rows that are above the new text area at the source.
Redisplay draws a new header line after the copy, so those rows are
still text.  No other message draws them at the destination."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (let ((steps (remoteterm-tests--header-toggle-steps
                  1.5 23 (list remoteterm-tests--toggle-header-line
                               remoteterm-tests--toggle-header-line))))
      (remoteterm-tests--check-scroll-steps steps)
      (pcase-let ((`(,copy (,old-top . ,_) (,new-top . ,_))
                   (remoteterm-tests--lower-window-copy (car (last steps)))))
        (should (< old-top new-top))
        (should copy)
        (should (< (plist-get copy :y) new-top))
        (should (= (- (plist-get copy :to-y) (plist-get copy :y))
                   (- new-top old-top)))))))

(defun remoteterm-tests--scroll-random-case ()
  "Return random arguments for `remoteterm-tests--scroll-layout-form'.
The changes are vertical scrolls, and a header line and a mode line
that come and go, over lines of different heights."
  (let ((height (rd-gen-element '(1.5 2.0 4.0 5.0)))
        (spacing (rd-gen-element '(nil 0 3 5))))
    (list
     `(((height . ,(rd-gen-element '(16 30))))
       (put-text-property 1 5 'face '(:height ,height))
       (setq-local make-cursor-line-fully-visible nil
                   line-spacing ,spacing)
       (setq header-line-format ,(rd-gen-element '(nil "H")))
       ,@(when (rd-gen-element '(nil t))
           '((setq window (split-window window 8))
             (set-window-buffer window buffer))))
     (cl-loop repeat (rd-gen-integer 2 6)
              collect (rd-gen-element
                       (list (remoteterm-tests--vscroll-form
                              (rd-gen-integer 0 40))
                             remoteterm-tests--toggle-header-line
                             remoteterm-tests--toggle-mode-line)))
     (rd-gen-element '(nil t)))))

(rd-daemon-deftest remoteterm-tests-scroll-bounds-random
  "Law 9 holds for random layouts and changes of the text area."
  (let ((rd-gen-trials 60)
        (copies 0))
    (rd-gen-for-all
     #'remoteterm-tests--scroll-random-case
     (lambda (case)
       (cl-incf copies
                (remoteterm-tests--check-scroll-steps
                 (rd-daemon-eval
                  (apply #'remoteterm-tests--scroll-layout-form case))))))
    (should (> copies 0))))

(defconst remoteterm-tests--cursor-kinds
  '((box . 0) (hollow . 1) (bar . 2) (hbar . 3))
  "The `cursor-type' symbols and the kinds of the draw-cursor message.")

(defun remoteterm-tests--cursor-type ()
  "Return a random value for `cursor-type'."
  (rd-gen-element (list 'box 'hollow
                        (cons 'bar (rd-gen-integer 1 12))
                        (cons 'hbar (rd-gen-integer 1 30)))))

(defun remoteterm-tests--cursor-form (text from to type &optional spacing)
  "Return a form for the daemon that moves a cursor in a line of TEXT.
Point is first at the index FROM of TEXT, and then at the index TO.
TYPE is the value of `cursor-type', and SPACING is the value of
`line-spacing'.  The value of the form is a plist.  :from-output and
:to-output are the bytes that the session sent for each position.
:codes is the vector of the glyph codes of TEXT.  :geometry is as for
`remoteterm-tests--geometry-form'."
  (remoteterm-tests--frame-form
   '((width . 60))
   `(progn
      (insert ,text)
      (setq cursor-type ',type
            line-spacing ,spacing)
      (set-window-point window ,(1+ from)))
   `(progn
      (set-window-point window ,(1+ to))
      (redisplay t)
      (list :from-output output
            :to-output (remote-display--take-output display)
            :codes ,(remoteterm-tests--codes-form text)
            :geometry ,(remoteterm-tests--geometry-form)))))

(defun remoteterm-tests--last-cursor (bytes)
  "Return (KIND X Y WIDTH HEIGHT) of the last draw-cursor in BYTES."
  (let ((fields (cdar (last (seq-filter
                             (lambda (message)
                               (eq (car message) 'draw-cursor))
                             (remoteterm-tests--messages bytes))))))
    (mapcar (lambda (name) (plist-get fields name))
            '(:kind :x :y :width :height))))

(defun remoteterm-tests--expected-cursor (type column geometry spacing)
  "Return (KIND X Y WIDTH HEIGHT) for a cursor of TYPE at COLUMN.
The cursor is on the first line, and the line spacing is SPACING.  A
box covers the line without the spacing, and a bar covers the whole
row.  GEOMETRY is as for `remoteterm-tests--geometry-form'."
  (pcase-let* (((map :x0 :top :char-width :line-height) geometry)
               (kind (cdr (assq (or (car-safe type) type)
                                remoteterm-tests--cursor-kinds)))
               (x (+ x0 (* column char-width)))
               (row-height (+ line-height spacing)))
    (pcase type
      (`(bar . ,width)
       (list kind x top (min width char-width) row-height))
      (`(hbar . ,height)
       (let ((height (min height row-height)))
         (list kind x (+ top (- row-height height)) char-width height)))
      (_ (list kind x top char-width line-height)))))

(defun remoteterm-tests--cursor-case ()
  "Return random arguments for `remoteterm-tests--cursor-form'.
The two positions are different."
  (let* ((length (rd-gen-integer 2 40))
         (from (rd-gen-random length)))
    (list (apply #'string (cl-loop repeat length
                                   collect (rd-gen-integer ?a ?z)))
          from
          (mod (+ from 1 (rd-gen-random (1- length))) length)
          (remoteterm-tests--cursor-type)
          (rd-gen-integer 0 12))))

(rd-daemon-deftest remoteterm-tests-cursor
  "The last draw-cursor message has the kind and the rectangle of the type."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     #'remoteterm-tests--cursor-case
     (lambda (case)
       (pcase-let* ((`(,_ ,from ,to ,type ,spacing) case)
                    ((map :from-output :to-output :geometry)
                     (rd-daemon-eval
                      (apply #'remoteterm-tests--cursor-form case))))
         (should (equal (remoteterm-tests--last-cursor from-output)
                        (remoteterm-tests--expected-cursor
                         type from geometry spacing)))
         (should (equal (remoteterm-tests--last-cursor to-output)
                        (remoteterm-tests--expected-cursor
                         type to geometry spacing)))
         (should (= (cl-count 'draw-cursor
                              (remoteterm-tests--messages to-output)
                              :key #'car)
                    1)))))))

(rd-daemon-deftest remoteterm-tests-cursor-erase
  "After point moves, the glyph under the old cursor is on the screen."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     #'remoteterm-tests--cursor-case
     (lambda (case)
       (pcase-let* ((`(,text ,from . ,_) case)
                    ((map :to-output :codes :geometry)
                     (rd-daemon-eval
                      (apply #'remoteterm-tests--cursor-form case)))
                    (row (aref (remoteterm-tests--replay
                                (remoteterm-tests--messages to-output)
                                1 (length text) geometry)
                               0)))
         (should (eql (aref row from) (aref codes from))))))))

(ert-deftest remoteterm-tests-no-cursor ()
  "A window with a nil `cursor-type' sends no draw-cursor message."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let (((map :from-output)
                 (rd-daemon-eval
                  (remoteterm-tests--cursor-form "abc" 0 1 nil))))
      (should-not (assq 'draw-cursor
                        (remoteterm-tests--messages from-output))))))

(ert-deftest remoteterm-tests-no-cursor-width ()
  "After a cursor goes away, `window-cursor-info' gives a width of 0."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should
     (= 0 (rd-daemon-eval
           (remoteterm-tests--frame-form
            nil
            '(insert "abc")
            '(progn
               (with-current-buffer buffer (setq cursor-type nil))
               (force-mode-line-update)
               (redisplay t)
               (aref (window-cursor-info window) 3))))))))

(defun remoteterm-tests--placed-cursor-form (parameters setup)
  "Return a form for the daemon that draws a cursor on a line of letters.
PARAMETERS is added to the parameters of the frame, and SETUP is a form
that runs with the buffer current.  Point is at column 2 of the line.
The value of the form is a plist with :output and :geometry."
  (remoteterm-tests--frame-form
   (cons '(width . 60) parameters)
   `(progn
      (insert "abcdef")
      (set-window-point window 3)
      ,setup)
   `(list :output output :geometry ,(remoteterm-tests--geometry-form))))

(defun remoteterm-tests--placed-cursor (parameters setup)
  "Return (RECTANGLE . GEOMETRY) for a cursor at column 2.
RECTANGLE is (KIND DX DY WIDTH HEIGHT), or nil if no cursor was drawn.
DX and DY are the distances of the cursor from the left edge of column
2 and the top of the line.  GEOMETRY is as for
`remoteterm-tests--geometry-form'.  PARAMETERS and SETUP are as for
`remoteterm-tests--placed-cursor-form'."
  (pcase-let* (((map :output (:geometry geometry))
                (rd-daemon-eval
                 (remoteterm-tests--placed-cursor-form parameters setup)))
               ((map :x0 :top :char-width) geometry)
               (`(,kind ,x ,y ,width ,height)
                (remoteterm-tests--last-cursor output)))
    (cons (and kind (list kind (- x x0 (* 2 char-width)) (- y top)
                          width height))
          geometry)))

(ert-deftest remoteterm-tests-cursor-rectangles ()
  "Examples of the rectangle of a bar and an hbar, computed by hand."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let* ((`(,_ . ,(map :char-width :line-height))
                  (remoteterm-tests--placed-cursor nil nil))
                 (w char-width)
                 (h line-height))
      (dolist (example
               `(((bar . 3) 0 (2 0 0 3 ,h))
                 ((bar . 1) 4 (2 0 0 1 ,(+ h 4)))
                 ;; A bar is not wider than the glyph.
                 ((bar . ,(1+ w)) 0 (2 0 0 ,w ,h))
                 ((hbar . 5) 0 (3 0 ,(- h 5) ,w 5))
                 ;; The spacing is below the line, and in the row.
                 ((hbar . 5) 4 (3 0 ,(- (+ h 4) 5) ,w 5))
                 ;; An hbar is not taller than the row.
                 ((hbar . ,(+ h 9)) 4 (3 0 0 ,w ,(+ h 4)))))
        (pcase-let ((`(,type ,spacing ,expected) example))
          (ert-info ((format "%S" example) :prefix "Example: ")
            (should (equal
                     (car (remoteterm-tests--placed-cursor
                           nil `(setq cursor-type ',type
                                      line-spacing ,spacing)))
                     expected))))))))

(ert-deftest remoteterm-tests-frame-cursor-type ()
  "The `cursor-type' frame parameter gives the cursor of a window."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let ((`(,box . ,(map :char-width))
                 (remoteterm-tests--placed-cursor nil nil))
                (`(,bar . ,_)
                 (remoteterm-tests--placed-cursor '((cursor-type . (bar . 3)))
                                                  nil))
                (`(,hbar . ,_)
                 (remoteterm-tests--placed-cursor '((cursor-type . (hbar . 3)))
                                                  nil)))
      (should (equal (seq-take box 2) '(0 0)))
      (should (equal (seq-take bar 4) '(2 0 0 3)))
      (should (equal (list (nth 0 hbar) (nth 3 hbar) (nth 4 hbar))
                     (list 3 char-width 3))))))

(ert-deftest remoteterm-tests-cursor-no-glyph ()
  "A cursor at a column with no glyph on a short line sends no message.
The line is shorter than the window, so there is also no fringe cursor."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-dolist (`(,direction ,hpos) '((left-to-right 1000)
                                         (right-to-left -1)))
      (pcase-let
          (((map :at-glyph :past-glyphs)
            (rd-daemon-eval
             (remoteterm-tests--frame-form
              nil
              `(progn
                 (setq bidi-paragraph-direction ',direction)
                 (insert "abc"))
              `(progn
                 (remote-display--take-output display)
                 (remote-display--draw-cursor window 1)
                 (let ((at-glyph (remote-display--take-output display)))
                   (remote-display--draw-cursor window ,hpos)
                   (list :at-glyph at-glyph
                         :past-glyphs
                         (remote-display--take-output display))))))))
        (ert-info ((symbol-name direction) :prefix "Direction: ")
          (should (assq 'draw-cursor (remoteterm-tests--messages at-glyph)))
          (should-not (remoteterm-tests--messages past-glyphs)))))))

(defun remoteterm-tests--unfocus-form ()
  "Return a form that leaves the session of FRAME with no focus frame.
The form gives the focus to a new frame, and deletes that frame."
  '(let ((other (x-create-frame-with-faces '((display . "remote-draw")))))
     (remote-display--focus other)
     (delete-frame other)))

(ert-deftest remoteterm-tests-cursor-no-focus ()
  "A frame with no focus shows the cursor of a window that is not selected."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let* ((`(,_ . ,(map :char-width :line-height))
                  (remoteterm-tests--placed-cursor nil nil))
                 (w char-width)
                 (h line-height))
      (dolist (example
               `((t t (1 0 0 ,w ,h))
                 (box t (1 0 0 ,w ,h))
                 (hollow t (1 0 0 ,w ,h))
                 ;; A bar loses one pixel of width, to at least 1.
                 ((bar . 3) t (2 0 0 2 ,h))
                 ((bar . 1) t (2 0 0 1 ,h))
                 ((hbar . 4) t (3 0 ,(- h 4) ,w 4))
                 ;; Any other value of the option replaces the type.
                 (box nil nil)
                 (box hollow (1 0 0 ,w ,h))
                 (box (bar . 3) (2 0 0 3 ,h))
                 (box (hbar . 2) (3 0 ,(- h 2) ,w 2))
                 (nil t nil)))
        (pcase-let ((`(,type ,option ,expected) example))
          (ert-info ((format "%S" example) :prefix "Example: ")
            (should (equal
                     (car (remoteterm-tests--placed-cursor
                           nil
                           `(progn
                              (setq cursor-type ',type
                                    cursor-in-non-selected-windows ',option)
                              ,(remoteterm-tests--unfocus-form))))
                     expected))))))))

(ert-deftest remoteterm-tests-cursor-non-selected-window ()
  "A frame with the focus shows a hollow cursor in the other window."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let*
        (((map :output :other-top)
          (rd-daemon-eval
           (remoteterm-tests--frame-form
            '((height . 20))
            '(let ((other (split-window window 8)))
               (insert "abc")
               (set-window-buffer other buffer)
               (set-window-point other 2)
               (set-window-point window 1))
            '(list :output output
                   :other-top
                   (nth 1 (window-inside-pixel-edges
                           (next-window window)))))))
         (cursors (seq-filter (lambda (message)
                                (eq (car message) 'draw-cursor))
                              (remoteterm-tests--messages output)))
         (kind-of (lambda (y)
                    (mapcar (lambda (message)
                              (plist-get (cdr message) :kind))
                            (seq-filter (lambda (message)
                                          (eq (= (plist-get (cdr message) :y)
                                                 other-top)
                                              y))
                                        cursors)))))
      (should (equal (funcall kind-of nil) '(0)))
      (should (equal (funcall kind-of t) '(1))))))

(defun remoteterm-tests--bidi-rectangle (type direction text position)
  "Return (KIND X Y WIDTH HEIGHT) of a cursor of TYPE on TEXT.
DIRECTION is the value of `bidi-paragraph-direction', and POSITION is
the position of point."
  (remoteterm-tests--last-cursor
   (rd-daemon-eval
    (remoteterm-tests--frame-form
     '((width . 40))
     `(progn
        (setq bidi-paragraph-direction ',direction
              cursor-type ',type)
        (insert ,text)
        (set-window-point window ,position))
     'output))))

(defun remoteterm-tests--right-to-left-rectangle (type)
  "Return (KIND X Y WIDTH HEIGHT) of a cursor of TYPE on a right-to-left glyph.
The glyph is the first of a line of Hebrew letters."
  (remoteterm-tests--bidi-rectangle type 'right-to-left "אבג" 1))

(ert-deftest remoteterm-tests-cursor-right-to-left-rectangles ()
  "Examples of a cursor on a right-to-left glyph, computed by hand."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let ((`(,_ ,x ,y ,w ,h)
                 (remoteterm-tests--right-to-left-rectangle 'box)))
      (should (> w 2))
      (dolist (example
               `(;; A bar is at the right edge of the glyph.
                 ((bar . 2) (2 ,(- (+ x w) 2) ,y 2 ,h))
                 ;; A bar as wide as the glyph covers the glyph.
                 ((bar . ,(1+ w)) (2 ,x ,y ,w ,h))
                 ;; An hbar and a hollow box cover the width of the glyph.
                 ((hbar . 3) (3 ,x ,(+ y (- h 3)) ,w 3))
                 (hollow (1 ,x ,y ,w ,h))))
        (ert-info ((format "%S" example) :prefix "Example: ")
          (should (equal (remoteterm-tests--right-to-left-rectangle
                          (car example))
                         (cadr example))))))))

(ert-deftest remoteterm-tests-cursor-level-of-glyph ()
  "The level of the glyph, not the paragraph, sets the edge of a bar.
A left-to-right paragraph can have a right-to-left glyph, and a
right-to-left paragraph can have a left-to-right glyph."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-dolist (`(,direction ,text ,position ,edge)
                   '((left-to-right "abc אבג" 5 right)
                     (left-to-right "abc אבג" 1 left)
                     (right-to-left "אבג abc" 5 left)
                     (right-to-left "abc" 1 left)
                     (right-to-left "אבג" 1 right)))
      (pcase-let* ((`(,_ ,x ,y ,w ,h)
                    (remoteterm-tests--bidi-rectangle 'box direction text
                                                      position))
                   (bar-x (if (eq edge 'right) (- (+ x w) 2) x)))
        (ert-info ((format "%S %S at %S" direction text position)
                   :prefix "Case: ")
          (should (> w 2))
          (should (equal (remoteterm-tests--bidi-rectangle
                          '(bar . 2) direction text position)
                         (list 2 bar-x y 2 h))))))))

(defconst remoteterm-tests--segments '("a" "b" "\t" "é" "ö")
  "Parts of a text: letters, a tab, and letters that compose with a mark.")

(defun remoteterm-tests--erase-case ()
  "Return (SEGMENTS FROM TO TYPE) for `remoteterm-tests--erase-form'.
FROM and TO are different indexes of SEGMENTS."
  (let* ((length (rd-gen-integer 2 8))
         (from (rd-gen-random length)))
    (list (cl-loop repeat length
                   collect (rd-gen-element remoteterm-tests--segments))
          from
          (mod (+ from 1 (rd-gen-random (1- length))) length)
          (remoteterm-tests--cursor-type))))

(defun remoteterm-tests--erase-form (segments from to type)
  "Return a form for the daemon that moves a cursor between SEGMENTS.
Point is first at the start of the segment at index FROM, and then at
the start of the segment at index TO.  TYPE is the value of
`cursor-type'.  The value of the form is a plist: :from-output and
:to-output are the bytes that the session sent for each position."
  (let ((position (lambda (index)
                    (1+ (length (apply #'concat (seq-take segments index)))))))
    (remoteterm-tests--frame-form
     '((width . 80))
     `(progn
        (insert ,(apply #'concat segments))
        (setq cursor-type ',type)
        (set-window-point window ,(funcall position from)))
     `(progn
        (set-window-point window ,(funcall position to))
        (redisplay t)
        (list :from-output output
              :to-output (remote-display--take-output display))))))

(defun remoteterm-tests--clears-p (x width message)
  "Return non-nil if MESSAGE clears WIDTH pixels from X."
  (let ((fields (cdr message)))
    (and (eq (car message) 'clear-area)
         (<= (plist-get fields :x) x
             (+ x width)
             (+ (plist-get fields :x) (plist-get fields :width))))))

(rd-daemon-deftest remoteterm-tests-cursor-erase-any-glyph
  "A clear covers an old cursor, also on a tab or a composition."
  (let ((rd-gen-trials 30))
    (rd-gen-for-all
     #'remoteterm-tests--erase-case
     (lambda (case)
       (pcase-let* (((map :from-output :to-output)
                     (rd-daemon-eval
                      (apply #'remoteterm-tests--erase-form case)))
                    (`(,_ ,x ,_ ,width ,_)
                     (remoteterm-tests--last-cursor from-output)))
         (should
          (cl-some (lambda (message)
                     (remoteterm-tests--clears-p x width message))
                   (remoteterm-tests--messages to-output))))))))

(ert-deftest remoteterm-tests-cursor-erase-glyphless ()
  "A clear covers an old cursor on a glyphless character."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let* (((map :from-output :to-output)
                  (rd-daemon-eval
                   (remoteterm-tests--frame-form
                    nil
                    '(progn
                       (setq-local glyphless-char-display
                                   (make-char-table 'glyphless-char-display))
                       (aset glyphless-char-display ?q 'empty-box)
                       (insert "qa")
                       (set-window-point window 1))
                    '(progn
                       (set-window-point window 2)
                       (redisplay t)
                       (list :from-output output
                             :to-output (remote-display--take-output
                                         display))))))
                 (`(,_ ,x ,_ ,width ,_)
                  (remoteterm-tests--last-cursor from-output)))
      (should (cl-some (lambda (message)
                         (remoteterm-tests--clears-p x width message))
                       (remoteterm-tests--messages to-output))))))

(defun remoteterm-tests--replace-form (old new &rest values)
  "Return a form for the daemon that draws OLD, and then NEW in its place.
The value of the form is a plist.  :output is only the bytes that the
session sent for NEW, and :geometry is as for
`remoteterm-tests--geometry-form'.  VALUES is more properties and forms
for their values."
  (remoteterm-tests--frame-form
   `((width . ,(+ (length old) 20)))
   `(insert ,old)
   `(progn
      (with-current-buffer buffer (erase-buffer) (insert ,new))
      (redisplay t)
      (list :output (remote-display--take-output display)
            :geometry ,(remoteterm-tests--geometry-form)
            ,@values))))

(rd-daemon-deftest remoteterm-tests-stretch-clear
  "A tab that replaces text clears each column of the tab."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     (lambda () (cons (rd-gen-integer 0 20) (rd-gen-integer 1 20)))
     (lambda (lengths)
       (pcase-let* ((`(,before . ,after) lengths)
                    (tab-end (* 8 (1+ (/ before 8))))
                    (new (concat (make-string before ?x) "\t"
                                 (make-string after ?y)))
                    (columns (+ tab-end after))
                    ((map :output :geometry)
                     (rd-daemon-eval
                      (remoteterm-tests--replace-form
                       (make-string (+ columns 8) ?z) new)))
                    (row (aref (remoteterm-tests--replay
                                (remoteterm-tests--messages output)
                                1 columns geometry)
                               0)))
         (should (cl-every (lambda (state) (eq state 'clear))
                           (seq-subseq row before tab-end))))))))

(ert-deftest remoteterm-tests-stretch-clear-left-edge ()
  "The clear for a tab that scrolled to the left stays in the text area."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let* (((map :output :x0 :bottom)
                  (rd-daemon-eval
                   (remoteterm-tests--frame-form
                    '((width . 40))
                    '(progn
                       (insert "\tabcdefghijklmnop")
                       (set-window-hscroll window 3))
                    '(let ((edges (window-inside-pixel-edges window)))
                       (list :output output :x0 (nth 0 edges)
                             :bottom (nth 3 edges))))))
                 (clears (seq-filter
                          (lambda (message)
                            (and (eq (car message) 'clear-area)
                                 (< (plist-get (cdr message) :y) bottom)
                                 (> (+ (plist-get (cdr message) :x)
                                       (plist-get (cdr message) :width))
                                    x0)))
                          (remoteterm-tests--messages output))))
      (should clears)
      (should (cl-every (lambda (message)
                          (<= x0 (plist-get (cdr message) :x)))
                        clears)))))

(defun remoteterm-tests--partial-line-form (type)
  "Return a form for the daemon with point on a line visible in part.
The line has a tab, and TYPE is the value of `cursor-type'.  The value
of the form is a plist: :output is the bytes that the session sent, and
:bottom is the pixel position of the bottom of the text area."
  (remoteterm-tests--frame-form
   '((width . 40) (height . 6))
   `(progn
      (setq cursor-type ',type)
      (setq-local make-cursor-line-fully-visible nil)
      ,(remoteterm-tests--grow-frame-form 5)
      (dotimes (_ (window-body-height window)) (insert "ab\n"))
      (insert "\tab")
      (set-window-start window (point-min))
      (set-window-point window (- (point-max) 1)))
   '(list :output output
          :bottom (nth 3 (window-inside-pixel-edges window)))))

(ert-deftest remoteterm-tests-partial-line-stays-in-text-area ()
  "A clear and a cursor on a line visible in part stay above the mode line."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (dolist (type '(box hollow (bar . 2) (hbar . 2)))
      (pcase-let* (((map :output :bottom)
                    (rd-daemon-eval
                     (remoteterm-tests--partial-line-form type)))
                   (messages (remoteterm-tests--messages output))
                   (cursor (assq 'draw-cursor messages)))
        (ert-info ((format "%S" type) :prefix "Cursor type: ")
          (should (cl-some (lambda (message)
                             (and (eq (car message) 'clear-area)
                                  (< (plist-get (cdr message) :y) bottom)
                                  (> (plist-get (cdr message) :height) 0)))
                           messages))
          (dolist (message messages)
            (let ((y (plist-get (cdr message) :y)))
              (when (or (eq (car message) 'draw-cursor)
                        (and (eq (car message) 'clear-area) (< y bottom)))
                (should (<= (+ y (plist-get (cdr message) :height))
                            bottom)))))
          ;; The row of an hbar is below the 5 visible pixels of the line.
          (if (equal type '(hbar . 2))
              (should-not cursor)
            (should cursor)))))))

(ert-deftest remoteterm-tests-partial-line-hbar ()
  "An hbar on a line with 5 visible rows shows only those rows.
An hbar that ends at the top of those rows, and only touches them,
sends no message."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (let* ((geometry (cdr (remoteterm-tests--placed-cursor nil nil)))
           (line-height (plist-get geometry :line-height))
           (hidden (- line-height 5)))
      (should (> hidden 1))
      (dolist (height (list (1- hidden) hidden (1+ hidden) line-height))
        (pcase-let* (((map :output :bottom)
                      (rd-daemon-eval
                       (remoteterm-tests--partial-line-form
                        (cons 'hbar height))))
                     (fields (cdr (assq 'draw-cursor
                                        (remoteterm-tests--messages output))))
                     (visible (- height hidden)))
          (ert-info ((format "%S" height) :prefix "Height: ")
            (if (<= visible 0)
                (should-not fields)
              (should (equal (plist-get fields :height) visible))
              (should (equal (plist-get fields :y) (- bottom visible))))))))))

(defun remoteterm-tests--draw-glyphs (bytes)
  "Return the fields of the draw-glyphs messages in BYTES."
  (mapcar #'cdr (seq-filter (lambda (message) (eq (car message) 'draw-glyphs))
                            (remoteterm-tests--messages bytes))))

(defun remoteterm-tests--clip (fields)
  "Return the clip of the draw-glyphs FIELDS as (LEFT TOP RIGHT BOTTOM)."
  (pcase-let (((map :clip-x :clip-y :clip-width :clip-height) fields))
    (list clip-x clip-y (+ clip-x clip-width) (+ clip-y clip-height))))

(defun remoteterm-tests--inside-p (clip area)
  "Return non-nil if the rectangle CLIP is inside the rectangle AREA.
Each is a list (LEFT TOP RIGHT BOTTOM)."
  (pcase-let ((`(,left ,top ,right ,bottom) clip)
              (`(,area-left ,area-top ,area-right ,area-bottom) area))
    (and (<= area-left left right area-right)
         (<= area-top top bottom area-bottom))))

(ert-deftest remoteterm-tests-glyph-clip-partial-line ()
  "The glyphs of a line visible in part stop at the mode line.
Redisplay sends the mode line first, so the client must not draw the
lower part of the line over it."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let* (((map :output :bottom)
                  (rd-daemon-eval (remoteterm-tests--partial-line-form 'box)))
                 (runs (remoteterm-tests--draw-glyphs output))
                 (clips (mapcar #'remoteterm-tests--clip runs)))
      ;; The baseline of the last line is below the text area.
      (should (cl-some (lambda (run) (>= (plist-get run :y) (- bottom 5)))
                       (seq-filter (lambda (run)
                                     (< (plist-get run :clip-y) bottom))
                                   runs)))
      (should (member bottom (mapcar (lambda (clip) (nth 3 clip)) clips)))
      (dolist (clip clips)
        (should (or (<= (nth 3 clip) bottom) (<= bottom (nth 1 clip))))))))

(defun remoteterm-tests--wide-text-form (hscroll)
  "Return a form for the daemon with text wider than the window.
The value of the form is a plist: :output is the bytes that the session
sent, :left, :right, and :bottom are the pixel positions of the edges
of the text area, and :glyph-width is the width of a glyph.  Each
glyph is 3 times the height of the frame font, and the window scrolls
to the left by HSCROLL columns."
  (remoteterm-tests--frame-form
   '((width . 20) (height . 6))
   `(progn
      (setq truncate-lines t)
      (insert (propertize (make-string 40 ?W) 'face '(:height 3.0)))
      (set-window-point window 1)
      (set-window-hscroll window ,hscroll))
   '(let ((edges (window-inside-pixel-edges window)))
      (list :output output :left (nth 0 edges) :right (nth 2 edges)
            :bottom (nth 3 edges)
            :glyph-width (aref (aref (font-get-glyphs font 0 1 "W") 0) 4)))))

(defun remoteterm-tests--text-area-runs (bytes bottom)
  "Return the draw-glyphs fields in BYTES with a clip above BOTTOM."
  (seq-filter (lambda (run) (< (plist-get run :clip-y) bottom))
              (remoteterm-tests--draw-glyphs bytes)))

(ert-deftest remoteterm-tests-glyph-clip-sides ()
  "A wide glyph cut at an edge of the text area keeps out of the fringe."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let* (((map :output :left :bottom)
                  (rd-daemon-eval (remoteterm-tests--wide-text-form 1)))
                 (run (seq-find (lambda (run)
                                  (< (plist-get run :x) left))
                                (remoteterm-tests--text-area-runs output
                                                                  bottom))))
      (should run)
      (should (= (plist-get run :clip-x) left)))
    (pcase-let* (((map :output :right :bottom :glyph-width)
                  (rd-daemon-eval (remoteterm-tests--wide-text-form 0)))
                 (run (seq-find (lambda (run)
                                  (> (length (plist-get run :glyphs)) 1))
                                (remoteterm-tests--text-area-runs
                                 output bottom))))
      ;; The last glyph of the run goes past the edge of the text area.
      (should (> (+ (plist-get run :x)
                    (* (length (plist-get run :glyphs)) glyph-width))
                 right))
      (should (= (nth 2 (remoteterm-tests--clip run)) right)))))

(defun remoteterm-tests--clip-case ()
  "Return random arguments for `remoteterm-tests--clip-form'."
  (list (rd-gen-integer 5 12) (rd-gen-integer 0 15)
        (rd-gen-element '(nil "H")) (rd-gen-integer 0 25)
        (rd-gen-integer 0 3) (rd-gen-element '(nil t))))

(defun remoteterm-tests--clip-form (height extra header vscroll hscroll split)
  "Return a form for the daemon that draws a window and gives its areas.
The frame has HEIGHT lines and EXTRA more pixels.  HEADER is the header
line, VSCROLL and HSCROLL are the scrolls of the window, and SPLIT
is non-nil for a second window.  The value of the form is a plist.
:output is the bytes that the session sent for a redraw.  :areas is the
list of the rectangles that can hold glyphs, each as (LEFT TOP RIGHT
BOTTOM): the text area, the header line, and the mode line of each
window."
  (remoteterm-tests--frame-form
   `((height . ,height))
   `(progn
      (setq truncate-lines t header-line-format ,header)
      (setq-local make-cursor-line-fully-visible nil)
      ,(remoteterm-tests--grow-frame-form extra)
      (insert (propertize "tall\n" 'face '(:height 5.0)))
      (dotimes (_ 40)
        (insert (propertize (make-string 120 ?W) 'face '(:height 1.5)) "\n"))
      (when ,split
        (set-window-buffer (split-window window nil t) buffer))
      (set-window-start window 1)
      (set-window-point window 1)
      (set-window-hscroll window ,hscroll))
   `(progn
      (set-window-vscroll window ,vscroll t)
      (redraw-frame frame)
      (redisplay t)
      (list :output (remote-display--take-output display)
            :areas
            (mapcan
             (lambda (window)
               (pcase-let ((`(,left ,top ,right ,bottom)
                            (window-pixel-edges window))
                           (`(,_ ,text-top ,_ ,text-bottom)
                            (window-inside-pixel-edges window)))
                 (list (window-inside-pixel-edges window)
                       (list left top right text-top)
                       (list left text-bottom right bottom))))
             (window-list frame))))))

(rd-daemon-deftest remoteterm-tests-glyph-clip-random
  "The clip of each draw-glyphs message is inside one area of one window."
  (let ((rd-gen-trials 40)
        (runs 0))
    (rd-gen-for-all
     #'remoteterm-tests--clip-case
     (lambda (case)
       (pcase-let (((map :output :areas)
                    (rd-daemon-eval
                     (apply #'remoteterm-tests--clip-form case))))
         (dolist (run (remoteterm-tests--draw-glyphs output))
           (cl-incf runs)
           (let ((clip (remoteterm-tests--clip run)))
             (should (cl-some (lambda (area)
                                (remoteterm-tests--inside-p clip area))
                              areas)))))))
    (should (> runs 0))))

(defun remoteterm-tests--boxed-run (line-width)
  "Return the x of the draws of a text after a face with a box of LINE-WIDTH.
The text is two boxed letters and then two plain letters.  The value is
a plist: :boxed is the x of the boxed run, :plain is the x of the plain
run, and :x0 and :char-width are as in the geometry."
  (pcase-let* ((box `(:box (:line-width ,line-width)))
               ((map :output :boxed-code :plain-code
                     (:geometry (map :x0 :char-width)))
                (rd-daemon-eval
                 (remoteterm-tests--frame-form
                  '((width . 40))
                  `(insert (propertize "ab" 'face ',box) "cd")
                  `(list :output output
                         :geometry ,(remoteterm-tests--geometry-form)
                         :boxed-code (aref (aref (font-get-glyphs
                                                  font 0 1 "a")
                                                 0)
                                           3)
                         :plain-code (aref (aref (font-get-glyphs
                                                  font 0 1 "c")
                                                 0)
                                           3)))))
               (run-x (lambda (code)
                        (plist-get
                         (seq-find (lambda (run)
                                     (eql (aref (plist-get run :glyphs) 0)
                                          code))
                                   (remoteterm-tests--draw-glyphs output))
                         :x))))
    (list :boxed (funcall run-x boxed-code)
          :plain (funcall run-x plain-code)
          :x0 x0 :char-width char-width)))

(ert-deftest remoteterm-tests-box-left-line ()
  "The first glyph of a box starts after the left line of the box."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let (((map :boxed :plain :x0 :char-width)
                 (remoteterm-tests--boxed-run 3)))
      (should (= boxed (+ x0 3)))
      (should (= plain (+ x0 (* 2 char-width) (* 2 3)))))))

(ert-deftest remoteterm-tests-box-line-inside ()
  "A box line inside the glyph does not move the glyph."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let (((map :boxed :x0) (remoteterm-tests--boxed-run '(-3 . 1))))
      (should (= boxed x0)))))

(ert-deftest remoteterm-tests-box-long-run ()
  "A long run in a box keeps the distance between its parts."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let* ((count (+ remote-display--max-glyphs 8))
                 ((map :output :bottom (:geometry (map :x0 :char-width)))
                  (rd-daemon-eval
                   (remoteterm-tests--frame-form
                    `((width . ,(+ count 20)))
                    `(insert (propertize (make-string ,count ?a) 'face
                                         '(:box (:line-width 3))))
                    `(list :output output
                           :bottom (nth 3 (window-inside-pixel-edges window))
                           :geometry ,(remoteterm-tests--geometry-form)))))
                 (`(,first ,second . ,_)
                  (remoteterm-tests--text-area-runs output bottom)))
      (should (= (plist-get first :x) (+ x0 3)))
      (should (= (plist-get second :x)
                 (+ x0 3 (* remote-display--max-glyphs char-width)))))))

(ert-deftest remoteterm-tests-box-composition ()
  "A composition in a box starts after the left line of the box."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (dolist (setup '((progn (insert "ab   ")
                            (compose-region 1 3 [?q (Br . Bl) ?w])
                            (put-text-property 1 3 'face
                                               '(:box (:line-width 3))))
                     (insert (propertize "é" 'face
                                         '(:box (:line-width 3)))
                             "   ")))
      (pcase-let* (((map :output :bottom (:geometry (map :x0)))
                    (rd-daemon-eval
                     (remoteterm-tests--frame-form
                      '((width . 40)) setup
                      `(list :output output
                             :bottom (nth 3 (window-inside-pixel-edges window))
                             :geometry ,(remoteterm-tests--geometry-form))))))
        ;; The first run in the text area is the first line.
        (should (= (plist-get (car (remoteterm-tests--text-area-runs
                                    output bottom))
                              :x)
                   (+ x0 3)))))))

(defun remoteterm-tests--overhang-form (change)
  "Return a form for the daemon that makes CHANGE in a line of big italic text.
Each letter M goes past its column at both sides, so the draw of one
column needs the foreground of the columns next to it.  The form CHANGE
can use the variable WINDOW.  The value of the form is a plist: :output
is the bytes that the session sent for CHANGE, :x0 is the left edge of
the text area, and :width is the width of a column.  The frame has no
scroll bar, so the clears of the text are the only clears."
  (remoteterm-tests--frame-form
   '((width . 40) (vertical-scroll-bars . nil))
   `(progn
      (insert (propertize (make-string 6 ?M) 'face
                          '(:slant italic :height 2.0))
              "\n")
      (set-window-point window 3))
   `(let ((metrics (aref (font-get-glyphs (font-at 1 window) 0 1 "M") 0)))
      ,change
      (redisplay t)
      (list :output (remote-display--take-output display)
            :x0 (nth 0 (window-inside-pixel-edges window))
            :width (aref metrics 4)))))

(defun remoteterm-tests--check-overhang (change column)
  "Check that a draw of COLUMN after CHANGE keeps the columns to its left.
CHANGE is as for `remoteterm-tests--overhang-form'.  Redisplay draws
the neighbors of COLUMN too, but no clear goes left of COLUMN.  The
clear of COLUMN is the first one."
  (pcase-let* (((map :output :x0 :width)
                (rd-daemon-eval (remoteterm-tests--overhang-form change)))
               (left (+ x0 (* column width)))
               (clears (remoteterm-tests--clears output)))
    (should (= (plist-get (car clears) :x) left))
    (should (= (plist-get (car clears) :width) width))
    (should (cl-every (lambda (clear) (>= (plist-get clear :x) left)) clears))
    (should (cl-some (lambda (run) (< (plist-get run :x) left))
                     (remoteterm-tests--draw-glyphs output)))))

(ert-deftest remoteterm-tests-overhang-cursor ()
  "The erase of a cursor keeps the pixels of the columns next to it."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (remoteterm-tests--check-overhang '(set-window-point window 6) 2)))

(ert-deftest remoteterm-tests-overhang-change ()
  "A new letter keeps the pixels of the columns next to it."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (remoteterm-tests--check-overhang
     '(with-current-buffer (window-buffer window)
        (save-excursion
          (goto-char 5)
          (delete-char 1)
          (insert (propertize "H" 'face '(:slant italic :height 2.0)))))
     4)))

(ert-deftest remoteterm-tests-overlap-of-rows ()
  "A row drawn again for its neighbor rows sends glyphs and no clear.
Its clip holds its own row, so a clear would remove its other glyphs."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (let ((output (rd-daemon-eval
                   (remoteterm-tests--frame-form
                    nil '(insert "abc\n")
                    '(progn
                       (remote-display--draw-overlap window)
                       (remote-display--take-output display))))))
      (should (remoteterm-tests--draw-glyphs output))
      (should-not (remoteterm-tests--clears output)))))

(defun remoteterm-tests--right-edge (text type stretch)
  "Return the right edge of a cursor of TYPE on TEXT in a right-to-left line.
Point is at the start of TEXT.  STRETCH is the value of
`x-stretch-cursor'.  The value is a plist with :edge and :width."
  (pcase-let* ((output
                (rd-daemon-eval
                 (remoteterm-tests--frame-form
                  '((width . 40))
                  `(progn
                     (setq x-stretch-cursor ,stretch
                           bidi-paragraph-direction 'right-to-left
                           cursor-type ',type)
                     (insert ,text)
                     (set-window-point window 1))
                  'output)))
               (`(,_ ,x ,_ ,width ,_)
                (remoteterm-tests--last-cursor output)))
    (list :edge (+ x width) :width width)))

(defun remoteterm-tests--stretch-cursor (setup)
  "Return a stretched cursor on a space 20 columns wide, in a truncated line.
The form SETUP inserts the line and sets the window point.  The value
is a plist: :fields is the fields of the last draw-cursor message, and
:left, :right, and :char-width are the edges of the text area and the
width of a character."
  (pcase-let (((map :output :left :right :char-width)
               (rd-daemon-eval
                (remoteterm-tests--frame-form
                 '((width . 40))
                 `(progn
                    (setq truncate-lines t
                          x-stretch-cursor t
                          auto-hscroll-mode nil)
                    ,setup)
                 '(let ((edges (window-inside-pixel-edges window)))
                    (list :output output
                          :left (nth 0 edges)
                          :right (nth 2 edges)
                          :char-width (frame-char-width frame)))))))
    (list :fields (cdr (assq 'draw-cursor
                             (reverse (remoteterm-tests--messages output))))
          :left left :right right :char-width char-width)))

(defconst remoteterm-tests--wide-space
  '(propertize " " 'display '(space :width 20))
  "A form that gives a space 20 columns wide.")

(ert-deftest remoteterm-tests-cursor-stops-at-right-edge ()
  "A cursor on a wide glyph that crosses the right edge stays in the text area."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let (((map :fields :right :char-width)
                 (remoteterm-tests--stretch-cursor
                  `(progn
                     (insert (make-string (- (window-body-width window) 2) ?a)
                             ,remoteterm-tests--wide-space "b")
                     (set-window-point window (- (point-max) 2))))))
      (should (= (plist-get fields :x) (- right (* 2 char-width))))
      (should (= (plist-get fields :width) (* 2 char-width))))))

(ert-deftest remoteterm-tests-cursor-stops-at-left-edge ()
  "A bar at the right of a glyph that crosses the left edge stays in the text.
The line goes from right to left.  A first space leaves one column for
the wide space, and the bar is wider than that column."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let (((map :fields :left :char-width)
                 (remoteterm-tests--stretch-cursor
                  `(let ((first-width (- (window-body-width window t)
                                         (frame-char-width frame))))
                     (setq bidi-paragraph-direction 'right-to-left
                           cursor-type (cons 'bar
                                             (+ (frame-char-width frame) 4)))
                     (insert (propertize " " 'display
                                         (list 'space :width
                                               (list first-width)))
                             ,remoteterm-tests--wide-space)
                     (set-window-point window 2)))))
      (should (= (plist-get fields :x) left))
      (should (= (plist-get fields :width) char-width)))))

(ert-deftest remoteterm-tests-cursor-right-to-left ()
  "A cursor on a right-to-left glyph is at the right edge of the glyph."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (let ((glyph-edge (plist-get (remoteterm-tests--right-edge "אב" 'box nil)
                                 :edge))
          (tab-edge (plist-get (remoteterm-tests--right-edge "\tא" 'box t)
                               :edge)))
      (should (> (plist-get (remoteterm-tests--right-edge "\tא" 'box t) :width)
                 (plist-get (remoteterm-tests--right-edge "\tא" 'box nil)
                            :width)))
      (dolist (type '(box hollow (bar . 2) (hbar . 2)))
        (ert-info ((format "%S" type) :prefix "Cursor type: ")
          (should (= (plist-get (remoteterm-tests--right-edge "אב" type nil)
                                :edge)
                     glyph-edge))
          (should (= (plist-get (remoteterm-tests--right-edge "\tא" type nil)
                                :edge)
                     tab-edge)))))))

(defun remoteterm-tests--composition-form (text)
  "Return a form for the daemon that draws TEXT, a composition, and spaces.
The value of the form is a plist with :output, :geometry, and :glyphs.
:glyphs has (CODE WIDTH ADJUSTMENT) for each glyph of the composition."
  (remoteterm-tests--frame-form
   '((width . 40))
   `(progn (insert ,text "   ")
           (set-window-point window (point-max)))
   `(let ((gstring (nth 2 (with-current-buffer buffer
                            (find-composition 1 nil nil t)))))
      (list :output output
            :geometry ,(remoteterm-tests--geometry-form)
            :glyphs
            (cl-loop for i below (lgstring-glyph-len gstring)
                     for glyph = (lgstring-glyph gstring i)
                     when glyph
                     collect (list (lglyph-code glyph) (lglyph-width glyph)
                                   (lglyph-adjustment glyph)))))))

(defun remoteterm-tests--placed-glyphs (bytes)
  "Return (X Y GLYPH) for each glyph code of the draw-glyphs messages in BYTES.
Each glyph after the first of a message is at the X of its message."
  (cl-loop for message in (remoteterm-tests--messages bytes)
           when (eq (car message) 'draw-glyphs)
           append (let ((fields (cdr message)))
                    (mapcar (lambda (glyph)
                              (list (plist-get fields :x)
                                    (plist-get fields :y)
                                    glyph))
                            (plist-get fields :glyphs)))))

(defun remoteterm-tests--expected-glyphs (glyphs x y)
  "Return (X Y GLYPH) for GLYPHS as in `remoteterm-tests--composition-form'.
The pen starts at X, Y."
  (let ((result nil))
    (pcase-dolist (`(,code ,width ,adjustment) glyphs)
      (pcase adjustment
        (`[,xoff ,yoff ,wadjust]
         (push (list (+ x xoff) (+ y yoff) code) result)
         (setq x (+ x wadjust)))
        (_ (push (list x y code) result)
           (setq x (+ x width)))))
    (nreverse result)))

(ert-deftest remoteterm-tests-composition-automatic ()
  "The glyphs of an automatic composition keep their offsets."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (dolist (text '("é" "ö́" "क्ष"))
      (pcase-let* (((map :output :glyphs (:geometry (map :x0 :top :ascent)))
                    (rd-daemon-eval
                     (remoteterm-tests--composition-form text)))
                   (expected (remoteterm-tests--expected-glyphs
                              glyphs x0 (+ top ascent))))
        (ert-info (text :prefix "Text: ")
          (should expected)
          (dolist (glyph expected)
            (should (member glyph
                            (remoteterm-tests--placed-glyphs output)))))))))

(defun remoteterm-tests--adjusted-composition-form (text)
  "Return a form for the daemon that adjusts the composition TEXT.
TEXT has 3 characters.  The form draws TEXT, gives the first glyph of
its composition an adjustment in the cache of compositions, and draws
TEXT again.  The value is as for `remoteterm-tests--composition-form',
with only the bytes of the second draw."
  (remoteterm-tests--frame-form
   '((width . 40))
   `(progn (insert ,text "   ")
           (set-window-point window (point-max)))
   `(let ((gstring (with-current-buffer buffer
                     (composition-get-gstring 1 4 (font-at 1 window) nil))))
      (lglyph-set-adjustment (lgstring-glyph gstring 0) 1 -2 3)
      (redraw-frame frame)
      (redisplay t)
      (list :output (remote-display--take-output display)
            :geometry ,(remoteterm-tests--geometry-form)
            :glyphs
            (cl-loop for i below (lgstring-glyph-len gstring)
                     for glyph = (lgstring-glyph gstring i)
                     when glyph
                     collect (list (lglyph-code glyph) (lglyph-width glyph)
                                   (lglyph-adjustment glyph)))))))

(ert-deftest remoteterm-tests-composition-adjusted ()
  "A glyph with an adjustment has its offset, and moves the pen by it."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let* (((map :output :glyphs (:geometry (map :x0 :top :ascent)))
                  (rd-daemon-eval
                   (remoteterm-tests--adjusted-composition-form
                    "é́")))
                 (expected (remoteterm-tests--expected-glyphs
                            glyphs x0 (+ top ascent))))
      (should (> (length glyphs) 1))
      (should (vectorp (nth 2 (car glyphs))))
      (dolist (glyph expected)
        (should (member glyph (remoteterm-tests--placed-glyphs output)))))))

(defun remoteterm-tests--static-composition (components)
  "Return the drawing of the text \"ab\" composed with COMPONENTS.
The value is a plist with :output, :x0, :baseline, :char-width, and
the glyph codes :q and :w of the letters q and w and :space of a space."
  (rd-daemon-eval
   (remoteterm-tests--frame-form
    '((width . 40))
    `(progn
       (insert "ab   ")
       (compose-region 1 3 ,components)
       (set-window-point window (point-max)))
    `(let ((edges (window-inside-pixel-edges window))
           (codes (font-get-glyphs font 0 3 "qw ")))
       (list :output output
             :x0 (nth 0 edges)
             :baseline (+ (nth 1 edges) (aref (query-font font) 4))
             :char-width (frame-char-width frame)
             :q (aref (aref codes 0) 3)
             :w (aref (aref codes 1) 3)
             :space (aref (aref codes 2) 3))))))

(ert-deftest remoteterm-tests-composition-static ()
  "The components of a static composition keep their offsets."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let* (((map :output :x0 :baseline :char-width :q :w)
                  (remoteterm-tests--static-composition [?q (Br . Bl) ?w]))
                 (placed (remoteterm-tests--placed-glyphs output)))
      (should (member (list x0 baseline q) placed))
      (should (member (list (+ x0 char-width) baseline w) placed)))
    (pcase-let* (((map :output :q :w)
                  (remoteterm-tests--static-composition [?q (tc . bc) ?w]))
                 (placed (remoteterm-tests--placed-glyphs output))
                 (y-of (lambda (code)
                         (cl-loop for (_ y glyph) in placed
                                  when (= glyph code) minimize y))))
      (should (< (funcall y-of w) (funcall y-of q))))))

(ert-deftest remoteterm-tests-composition-static-two-fonts ()
  "Each component of a composition in two fonts reaches the client once.
Redisplay draws the components of each font in a glyph string of its
own, so the second glyph string does not start at the first component."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let* (((map :output :x0 :baseline :q :space)
                  (remoteterm-tests--static-composition [?q (Br . Bl) ?א]))
                 (placed (seq-filter
                          (lambda (glyph)
                            (and (= (cadr glyph) baseline)
                                 (<= x0 (car glyph))
                                 (/= (nth 2 glyph) space)))
                          (remoteterm-tests--placed-glyphs output))))
      (should (= (length placed) 2))
      (should (equal (car placed) (list x0 baseline q)))
      (should-not (= (nth 2 (cadr placed)) q)))))

(defun remoteterm-tests--cluster-form ()
  "Return a form for the daemon that moves the cursor off a cluster.
The shaper gives a composition of all letters, with one cluster for
each letter.  So the erase of the cursor draws only the cluster under
it, and that cluster is not the first one.
The value of the form is a plist.  :output is the bytes that the
session sent after the move of the cursor from the third letter, :x0,
:baseline and :char-width are as for
`remoteterm-tests--static-composition', and :a and :c are the glyph
codes of the first and the third letter."
  (remoteterm-tests--frame-form
   '((width . 40))
   '(let ((table (make-char-table nil)))
      (set-char-table-range
       table '(?a . ?z)
       (list (vector "[a-z]+" 0 (lambda (gstring direction)
                                  (font-shape-gstring gstring direction)))))
      (setq-local composition-function-table table)
      (insert "abcdef")
      (set-window-point window 3))
   '(let ((edges (window-inside-pixel-edges window))
          (codes (font-get-glyphs font 0 3 "abc")))
      (set-window-point window 6)
      (redisplay t)
      (list :output (remote-display--take-output display)
            :x0 (nth 0 edges)
            :baseline (+ (nth 1 edges) (aref (query-font font) 4))
            :char-width (frame-char-width frame)
            :a (aref (aref codes 0) 3)
            :c (aref (aref codes 2) 3)))))

(ert-deftest remoteterm-tests-composition-automatic-cluster ()
  "A glyph string for a later cluster sends only the glyphs of that cluster."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let* (((map :output :x0 :baseline :char-width :a :c)
                  (rd-daemon-eval (remoteterm-tests--cluster-form)))
                 (placed (remoteterm-tests--placed-glyphs output)))
      (should (member (list (+ x0 (* 2 char-width)) baseline c) placed))
      (should-not (cl-find a placed :key (lambda (glyph) (nth 2 glyph)))))))

(ert-deftest remoteterm-tests-composition-static-tab ()
  "A tab in a static composition is padding, and draws no glyph."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let* (((map :output :x0 :baseline :q :space)
                  (remoteterm-tests--static-composition "\tq\t"))
                 (line (seq-filter
                        (lambda (glyph)
                          (and (= (cadr glyph) baseline)
                               (<= x0 (car glyph))))
                        (remoteterm-tests--placed-glyphs output))))
      (should (equal (delq space (mapcar (lambda (glyph) (nth 2 glyph)) line))
                     (list q))))))

(defconst remoteterm-tests--fringe-width 8
  "The width of the fringe bitmap of the fringe tests.")

(defun remoteterm-tests--fringe-form (rows)
  "Return a form for the daemon that draws a fringe bitmap, and removes it.
ROWS is the vector of the rows of the bitmap.  The bitmap is on the
first line.  The value of the form is a plist.  :draw-output and
:remove-output are the bytes that the session sent for each step.
:geometry is as for `remoteterm-tests--geometry-form', and :fringe is
the width of the left fringe."
  (remoteterm-tests--frame-form
   '((width . 40))
   `(progn
      (define-fringe-bitmap 'remote-test ,rows nil
                            ,remoteterm-tests--fringe-width 'top)
      (insert (propertize "x" 'display '(left-fringe remote-test)) "abc\n"))
   `(progn
      (with-current-buffer buffer
        (remove-text-properties 1 2 '(display nil)))
      (redisplay t)
      (list :draw-output output
            :remove-output (remote-display--take-output display)
            :geometry ,(remoteterm-tests--geometry-form)
            :fringe (car (window-fringes window))))))

(defun remoteterm-tests--fringe-rows ()
  "Return a random vector of rows of 8 bits."
  (vconcat (cl-loop repeat (rd-gen-integer 1 12)
                    collect (rd-gen-random 256))))

(defun remoteterm-tests--fringes (bytes)
  "Return the fields of the draw-fringe messages in BYTES."
  (mapcar #'cdr (seq-filter (lambda (message)
                              (eq (car message) 'draw-fringe))
                            (remoteterm-tests--messages bytes))))

(rd-daemon-deftest remoteterm-tests-fringe
  "A bitmap is on its line, and in its fringe."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     #'remoteterm-tests--fringe-rows
     (lambda (rows)
       (pcase-let* (((map :draw-output :fringe (:geometry (map :x0 :top)))
                     (rd-daemon-eval
                      (remoteterm-tests--fringe-form rows)))
                    (fields (seq-find (lambda (fields)
                                        (equal (plist-get fields :rows)
                                               rows))
                                      (remoteterm-tests--fringes
                                       draw-output))))
         (should fields)
         (should (= (plist-get fields :y) top))
         (should (= (plist-get fields :width)
                    remoteterm-tests--fringe-width))
         (should (<= (- x0 fringe) (plist-get fields :x)))
         (should (<= (+ (plist-get fields :x)
                        (plist-get fields :width))
                     x0)))))))

(rd-daemon-deftest remoteterm-tests-fringe-erase
  "After a bitmap goes away, a clear covers it."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     #'remoteterm-tests--fringe-rows
     (lambda (rows)
       (pcase-let* (((map :draw-output :remove-output
                          (:geometry (map :top)))
                     (rd-daemon-eval
                      (remoteterm-tests--fringe-form rows)))
                    (fields (seq-find (lambda (fields)
                                        (equal (plist-get fields :rows)
                                               rows))
                                      (remoteterm-tests--fringes
                                       draw-output)))
                    (messages (remoteterm-tests--messages remove-output)))
         (should (= (plist-get fields :y) top))
         (should (remoteterm-tests--fringe-cleared-p fields messages)))))))

(defun remoteterm-tests--fringe-cleared-p (fields messages)
  "Return non-nil if one clear-area of MESSAGES covers a bitmap.
FIELDS is the fields of the draw-fringe message of the bitmap."
  (pcase-let (((map :x :y :width :rows) fields))
    (cl-some
     (lambda (message)
       (let ((clear (cdr message)))
         (and (eq (car message) 'clear-area)
              (<= (plist-get clear :x) x)
              (<= (+ x width) (+ (plist-get clear :x)
                                 (plist-get clear :width)))
              (<= (plist-get clear :y) y)
              (<= (+ y (length rows)) (+ (plist-get clear :y)
                                         (plist-get clear :height))))))
     messages)))

(defun remoteterm-tests--deleted-window-form (parameters setup)
  "Return a form for the daemon that draws a bitmap in a window, and deletes it.
PARAMETERS is added to the parameters of the frame, and the form SETUP
runs in the buffer of the window that stays.  The new window is below
that window.  The value of the form is a plist.  :drawn and :deleted
are the bytes that the session sent for each step."
  (remoteterm-tests--frame-form
   `((width . 40) (height . 20) ,@parameters)
   `(progn
      (define-fringe-bitmap 'remote-test [#xff #x81 #xff] nil 8 'top)
      (insert "upper\n")
      ,setup)
   '(let ((lower (split-window window))
          (lower-buffer (generate-new-buffer "lower")))
      (unwind-protect
          (progn
            (with-current-buffer lower-buffer
              (insert (propertize "x" 'display '(left-fringe remote-test))
                      "abc"))
            (set-window-buffer lower lower-buffer)
            (redisplay t)
            (let ((drawn (remote-display--take-output display)))
              (delete-window lower)
              (redisplay t)
              (list :drawn drawn
                    :deleted (remote-display--take-output display))))
        (kill-buffer lower-buffer)))))

(ert-deftest remoteterm-tests-fringe-deleted-window ()
  "The bitmap of a deleted window goes away from the window that grows."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let* (((map :drawn :deleted)
                  (rd-daemon-eval
                   (remoteterm-tests--deleted-window-form nil nil)))
                 (fields (seq-find (lambda (fields)
                                     (equal (plist-get fields :rows)
                                            [#xff #x81 #xff]))
                                   (remoteterm-tests--fringes drawn))))
      (should fields)
      (should (remoteterm-tests--fringe-cleared-p
               fields (remoteterm-tests--messages deleted))))))

(ert-deftest remoteterm-tests-no-internal-border ()
  "A remote frame has no internal border.
So a window that goes away leaves no rest in a border, and the hook
after a line of a window has no border to clear."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-border"
    (remoteterm-tests--with-frame frame "remote-border"
                                  '((internal-border-width . 8))
      (should (= (frame-internal-border-width frame) 0))
      (should (= (nth 0 (window-pixel-edges (frame-root-window frame))) 0)))))

(defun remoteterm-tests--overlay-arrow-form (rows)
  "Return a form for the daemon that draws an overlay arrow on a bitmap.
ROWS is the vector of the rows of a bitmap on the first line, under
the arrow.  The value of the form is the bytes that the session sent."
  (remoteterm-tests--frame-form
   '((width . 40))
   `(progn
      (define-fringe-bitmap 'remote-test ,rows nil 8 'top)
      (define-fringe-bitmap 'remote-arrow [#x18 #x18] nil 8 'center)
      (put 'overlay-arrow-position 'overlay-arrow-bitmap 'remote-arrow)
      (insert (propertize "x" 'display '(left-fringe remote-test)) "abc\n")
      (insert "abc")
      (setq overlay-arrow-position (copy-marker 1)))
   'output))

(rd-daemon-deftest remoteterm-tests-fringe-overlay
  "An overlay arrow has the overlay flag, and keeps the pixels under it."
  (let ((rd-gen-trials 10))
    (rd-gen-for-all
     #'remoteterm-tests--fringe-rows
     (lambda (rows)
       (let* ((messages (remoteterm-tests--messages
                         (rd-daemon-eval
                          (remoteterm-tests--overlay-arrow-form rows))))
              (bitmap (cl-position-if
                       (lambda (message)
                         (and (eq (car message) 'draw-fringe)
                              (equal (plist-get (cdr message) :rows) rows)))
                       messages))
              (arrow (cl-position-if
                      (lambda (message)
                        (and (eq (car message) 'draw-fringe)
                             (equal (plist-get (cdr message) :rows)
                                    [#x18 #x18])))
                      messages)))
         (should bitmap)
         (should arrow)
         (should (= (plist-get (cdr (nth bitmap messages)) :overlay) 0))
         (should (= (plist-get (cdr (nth arrow messages)) :overlay) 1))
         (should (< bitmap arrow))
         (should-not (assq 'clear-area
                           (seq-subseq messages bitmap arrow))))))))

(defun remoteterm-tests--full-line-form (direction start &optional short setup)
  "Return a form for the daemon that moves point to the end of a full line.
DIRECTION is the value of `bidi-paragraph-direction'.  The line is
SHORT characters narrower than the window, with a default of 0, and the
form START gives the first position of point.  The form SETUP runs
after these.  The value of the form is a plist.  :first-output and
:moved-output are the bytes that the session sent for each position of
point.  :left and :right are the pixel positions of the edges of the
text area."
  (remoteterm-tests--frame-form
   '((width . 30))
   `(progn
      (setq bidi-paragraph-direction ',direction)
      (insert (make-string (- (window-body-width window) ,(or short 0)) ?a))
      (set-window-point window ,start)
      ,setup)
   '(progn
      (set-window-point window (with-current-buffer buffer (point-max)))
      (redisplay t)
      (let ((edges (window-inside-pixel-edges window)))
        (list :first-output output
              :moved-output (remote-display--take-output display)
              :left (nth 0 edges)
              :right (nth 2 edges))))))

(defun remoteterm-tests--fringe-cursor (bytes)
  "Return the fields of the draw-fringe message of the cursor in BYTES."
  (seq-find (lambda (fields) (= (plist-get fields :cursor) 1))
            (remoteterm-tests--fringes bytes)))

(ert-deftest remoteterm-tests-fringe-cursor ()
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let* (((map :first-output :right)
                  (rd-daemon-eval
                   (remoteterm-tests--full-line-form 'left-to-right
                                                     '(point-max))))
                 (fields (remoteterm-tests--fringe-cursor first-output)))
      (should fields)
      (should (<= right (plist-get fields :x))))))

(ert-deftest remoteterm-tests-fringe-cursor-right-to-left ()
  "A row that goes from right to left has the fringe cursor at the left."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let* (((map :first-output :left)
                  (rd-daemon-eval
                   (remoteterm-tests--full-line-form 'right-to-left
                                                     '(point-max))))
                 (fields (remoteterm-tests--fringe-cursor first-output)))
      (should fields)
      (should (<= (+ (plist-get fields :x) (plist-get fields :width)) left)))))

(ert-deftest remoteterm-tests-fringe-cursor-moved ()
  "A move of point to the end of a full line draws only the fringe cursor."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (dolist (direction '(left-to-right right-to-left))
      (pcase-let* (((map :first-output :moved-output :left :right)
                    (rd-daemon-eval
                     (remoteterm-tests--full-line-form direction
                                                       '(point-min))))
                   (fields (remoteterm-tests--fringe-cursor moved-output)))
        (ert-info ((symbol-name direction) :prefix "Direction: ")
          (should-not (remoteterm-tests--fringe-cursor first-output))
          (should fields)
          (if (eq direction 'left-to-right)
              (should (<= right (plist-get fields :x)))
            (should (<= (+ (plist-get fields :x) (plist-get fields :width))
                        left))))))))

(ert-deftest remoteterm-tests-fringe-cursor-short-line ()
  "A line narrower than the window has its cursor in the text area."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (dolist (direction '(left-to-right right-to-left))
      (pcase-let (((map :first-output :moved-output)
                   (rd-daemon-eval
                    (remoteterm-tests--full-line-form direction '(point-max)
                                                      1))))
        (ert-info ((symbol-name direction) :prefix "Direction: ")
          (should-not (remoteterm-tests--fringe-cursor first-output))
          (should-not (remoteterm-tests--fringe-cursor moved-output))
          (should (assq 'draw-cursor
                        (remoteterm-tests--messages first-output))))))))

(ert-deftest remoteterm-tests-fringe-cursor-no-focus ()
  "A frame with no focus shows a hollow cursor in the fringe."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let*
        (((map (:first-output focused))
          (rd-daemon-eval
           (remoteterm-tests--full-line-form 'left-to-right '(point-max))))
         ((map (:first-output unfocused) :right)
          (rd-daemon-eval
           (remoteterm-tests--full-line-form
            'left-to-right '(point-max) nil
            (remoteterm-tests--unfocus-form))))
         (filled (remoteterm-tests--fringe-cursor focused))
         (hollow (remoteterm-tests--fringe-cursor unfocused)))
      (should filled)
      (should hollow)
      (should (<= right (plist-get hollow :x)))
      (should-not (equal (plist-get hollow :rows) (plist-get filled :rows))))))

(defun remoteterm-tests--narrow-fringe-form (rows bitmap-width fringe)
  "Return a form for the daemon that draws a bitmap in a narrow fringe.
ROWS is the vector of the rows of the bitmap, which is BITMAP-WIDTH
pixels wide.  Each fringe is FRINGE pixels wide.  The value of the
form is the bytes that the session sent."
  (remoteterm-tests--frame-form
   '((width . 40))
   `(progn
      (define-fringe-bitmap 'remote-test ,rows nil ,bitmap-width 'top)
      (insert (propertize "x" 'display '(left-fringe remote-test)) "abc")
      (set-window-fringes window ,fringe ,fringe))
   'output))

(defun remoteterm-tests--narrow-fringe-case ()
  "Return random arguments for `remoteterm-tests--narrow-fringe-form'."
  (let ((bitmap-width (rd-gen-integer 1 16)))
    (list (vconcat (cl-loop repeat (rd-gen-integer 1 12)
                            collect (rd-gen-random (ash 1 bitmap-width))))
          bitmap-width
          (rd-gen-integer 1 20))))

(rd-daemon-deftest remoteterm-tests-fringe-narrow
  "A fringe that is narrower than a bitmap shows the left pixels of it."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     #'remoteterm-tests--narrow-fringe-case
     (lambda (case)
       (pcase-let* ((`(,rows ,bitmap-width ,fringe) case)
                    (width (min bitmap-width fringe))
                    (cut (- bitmap-width width))
                    (output (rd-daemon-eval
                             (apply #'remoteterm-tests--narrow-fringe-form
                                    case))))
         (should (member (list width
                               (vconcat (mapcar (lambda (row) (ash row (- cut)))
                                                rows)))
                         (mapcar (lambda (fields)
                                   (list (plist-get fields :width)
                                         (plist-get fields :rows)))
                                 (remoteterm-tests--fringes output)))))))))

(defun remoteterm-tests--periodic-fringe-form (pattern line vscroll)
  "Return a form for the daemon that draws a periodic bitmap on line LINE.
PATTERN is the vector of the rows of one period.  The window has a
vertical scroll of VSCROLL pixels.  The value of the form is a plist
with :output, :actual-vscroll, and :geometry as for
`remoteterm-tests--geometry-form'."
  (remoteterm-tests--frame-form
   '((width . 40))
   `(progn
      (define-fringe-bitmap 'remote-test ,pattern nil nil '(top periodic))
      (dotimes (_ ,line) (insert "abc\n"))
      (insert (propertize "x" 'display '(left-fringe remote-test)) "abc\n")
      (dotimes (_ 5) (insert "abc\n"))
      (insert "abc")
      (set-window-point window (point-max))
      (set-window-vscroll window ,vscroll t))
   `(list :output output
          :geometry ,(remoteterm-tests--geometry-form)
          :actual-vscroll (window-vscroll window t))))

(defun remoteterm-tests--periodic-fringe-case ()
  "Return random arguments for `remoteterm-tests--periodic-fringe-form'."
  (list (vconcat (cl-loop repeat (rd-gen-integer 2 5)
                          collect (rd-gen-random 256)))
        (rd-gen-integer 0 2)
        (rd-gen-integer 0 7)))

(defun remoteterm-tests--periodic-rows-p (rows y pattern)
  "Return non-nil if ROWS, drawn at Y, repeat PATTERN at the phase of Y."
  (cl-loop for i below (length rows)
           always (= (aref rows i)
                     (aref pattern (mod (+ y i) (length pattern))))))

(rd-daemon-deftest remoteterm-tests-fringe-periodic
  "A periodic bitmap stays at its phase, also on a line above the frame."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     #'remoteterm-tests--periodic-fringe-case
     (lambda (case)
       (pcase-let*
           ((`(,pattern ,line ,vscroll) case)
            ((map :output :actual-vscroll
                  (:geometry (map :top :line-height)))
             (rd-daemon-eval
              (apply #'remoteterm-tests--periodic-fringe-form case)))
            (row-y (+ top (* line line-height) (- vscroll)))
            (fields (seq-find (lambda (fields)
                                (= (plist-get fields :cursor) 0))
                              (remoteterm-tests--fringes output))))
         (should (= actual-vscroll vscroll))
         (should fields)
         (let ((y (plist-get fields :y))
               (rows (plist-get fields :rows)))
           (should (<= (max row-y top) y))
           (should (< y (+ (max row-y top) (length pattern))))
           (should (<= (+ y (length rows)) (+ row-y line-height)))
           (should (remoteterm-tests--periodic-rows-p rows y pattern))))))))

(ert-deftest remoteterm-tests-fringe-above-frame ()
  "A periodic bitmap that starts above the frame keeps its phase."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let*
        ((pattern [#xff #x81])
         ((map :output (:geometry (map :top :line-height)))
          (rd-daemon-eval
           (remoteterm-tests--periodic-fringe-form pattern 0 5)))
         (fields (car (remoteterm-tests--fringes output)))
         (rows (plist-get fields :rows)))
      (should (< (- top 5) 0))
      (should fields)
      (should (= (plist-get fields :y) top))
      (should (= (length rows) (- line-height 5)))
      (should (remoteterm-tests--periodic-rows-p rows top pattern)))))

(defun remoteterm-tests--placed-fringe-form (side align rows width vscroll
                                                   &optional setup)
  "Return a form for the daemon that draws a bitmap on the first line.
SIDE is `left-fringe' or `right-fringe', ALIGN is the alignment of the
bitmap, and ROWS is the vector of its rows.  WIDTH is its width.  The
window has a vertical scroll of VSCROLL pixels.  The form SETUP runs
first in the buffer of the window.  The value of the form is a plist.
:output is the bytes that the session sent, :right is the right edge of
the text area, :fringes is the list of the widths of the fringes,
:actual-vscroll is the vertical scroll, and :geometry is as for
`remoteterm-tests--geometry-form'."
  (remoteterm-tests--frame-form
   '((width . 40))
   `(progn
      ,setup
      (define-fringe-bitmap 'remote-test ,rows nil ,width ',align)
      (insert (propertize "x" 'display '(,side remote-test)) "abc\n")
      (dotimes (_ 5) (insert "abc\n"))
      (insert "abc")
      (set-window-point window (point-max))
      (set-window-vscroll window ,vscroll t))
   `(list :output output
          :right (nth 2 (window-inside-pixel-edges window))
          :fringes (seq-take (window-fringes window) 2)
          :actual-vscroll (window-vscroll window t)
          :geometry ,(remoteterm-tests--geometry-form))))

(defun remoteterm-tests--placed-fringe (side align rows width
                                             &optional vscroll setup)
  "Return the one bitmap of `remoteterm-tests--placed-fringe-form'.
The value is the plist of that form, with the fields of the draw-fringe
message of the bitmap in :fields and the fields of each clear-area
message in :clears.  SIDE, ALIGN, ROWS, WIDTH, VSCROLL, and SETUP are as
for that form, and the default of VSCROLL is 0."
  (let* ((result (rd-daemon-eval
                  (remoteterm-tests--placed-fringe-form side align rows width
                                                        (or vscroll 0) setup)))
         (fringes (remoteterm-tests--fringes (plist-get result :output))))
    (should (= (length fringes) 1))
    (should (eql (plist-get result :actual-vscroll) (or vscroll 0)))
    (plist-put result :clears
               (remoteterm-tests--clears (plist-get result :output)))
    (plist-put result :fields (car fringes))))

(defun remoteterm-tests--clears (bytes)
  "Return the fields of the clear-area messages in BYTES."
  (mapcar #'cdr (seq-filter (lambda (message) (eq (car message) 'clear-area))
                            (remoteterm-tests--messages bytes))))

(ert-deftest remoteterm-tests-fringe-side ()
  "A bitmap is in the middle of its fringe, computed by hand for each side."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-dolist (`(,width ,inset) '((8 0) (4 2)))
      (pcase-let (((map :fringes (:fields left) (:geometry (map :x0)))
                   (remoteterm-tests--placed-fringe 'left-fringe 'top [#xf]
                                                    width))
                  ((map :right (:fields right-fields))
                   (remoteterm-tests--placed-fringe 'right-fringe 'top [#xf]
                                                    width)))
        (ert-info ((format "%S" width) :prefix "Width: ")
          (should (equal fringes '(8 8)))
          (should (= (plist-get left :x) (- x0 width inset)))
          (should (= (plist-get right-fields :x) (+ right inset)))
          (should (= (plist-get right-fields :width) width)))))))

(ert-deftest remoteterm-tests-fringe-plain-flags ()
  "A plain bitmap is not a cursor, and it is not an overlay."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let (((map (:fields fields))
                 (remoteterm-tests--placed-fringe 'left-fringe 'top [#xff] 8)))
      (should (= (plist-get fields :cursor) 0))
      (should (= (plist-get fields :overlay) 0)))))

(ert-deftest remoteterm-tests-fringe-align ()
  "Examples of the alignment of a bitmap in a line, computed by hand."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (dolist (side '(left-fringe right-fringe))
      (dolist (align '(top center bottom))
        (pcase-let* (((map (:fields fields)
                           (:geometry (map :top :line-height)))
                      (remoteterm-tests--placed-fringe
                       side align [#x11 #x22 #x44 #x88] 8))
                     (offset (pcase align
                               ('top 0)
                               ('center (/ (- line-height 4) 2))
                               ('bottom (- line-height 4)))))
          (ert-info ((format "%S %S" side align) :prefix "Bitmap: ")
            (should (= (plist-get fields :y) (+ top offset)))
            (should (equal (plist-get fields :rows)
                           [#x11 #x22 #x44 #x88]))))))))

(ert-deftest remoteterm-tests-fringe-taller-than-line ()
  "The rows of a bitmap outside its line do not reach the client."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (let ((rows (vconcat (number-sequence 100 129))))
      (dolist (align '(top center bottom))
        (pcase-let* (((map (:fields fields) :clears
                           (:geometry (map :top :line-height)))
                      (remoteterm-tests--placed-fringe 'left-fringe align
                                                       rows 8))
                     (first (pcase align
                              ('top 0)
                              ('center (/ (- 30 line-height) 2))
                              ('bottom (- 30 line-height)))))
          (ert-info ((symbol-name align) :prefix "Alignment: ")
            (should (< line-height 30))
            (should (= (plist-get fields :y) top))
            ;; A bitmap that covers the row has no area to clear.
            (should-not (cl-find-if
                         (lambda (clear) (< (plist-get clear :x) 0))
                         clears))
            (should (equal (plist-get fields :rows)
                           (seq-subseq rows first
                                       (+ first line-height))))))))))

(ert-deftest remoteterm-tests-fringe-above-text-area ()
  "The rows of a bitmap above the top of the text area do not reach the client."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let (((map (:fields fields) (:geometry (map :top)))
                 (remoteterm-tests--placed-fringe
                  'left-fringe 'top [1 2 3 4 5 6 7 8 9 10 11 12] 8 5)))
      (should (= (plist-get fields :y) top))
      (should (equal (plist-get fields :rows) [6 7 8 9 10 11 12])))))

(ert-deftest remoteterm-tests-fringe-below-header-line ()
  "The rows of a bitmap above the text area stay off the header line."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let (((map (:fields fields) (:geometry (map :top)))
                 (remoteterm-tests--placed-fringe
                  'left-fringe 'top [1 2 3 4 5 6 7 8 9 10 11 12] 8 5
                  '(setq header-line-format "header"))))
      (should (< 0 top))
      (should (= (plist-get fields :y) top))
      (should (equal (plist-get fields :rows) [6 7 8 9 10 11 12])))))

(defun remoteterm-tests--partial-fringe-form (rows)
  "Return a form for the daemon with a bitmap on a line visible in part.
ROWS is the vector of the rows of the bitmap.  The last line of the
text area shows only 5 pixels.  The value of the form is a plist:
:output is the bytes that the session sent, and :bottom is the pixel
position of the bottom of the text area."
  (remoteterm-tests--frame-form
   '((width . 40) (height . 6))
   `(progn
      (define-fringe-bitmap 'remote-test ,rows nil 8 'top)
      (setq-local make-cursor-line-fully-visible nil)
      ,(remoteterm-tests--grow-frame-form 5)
      (dotimes (_ (window-body-height window)) (insert "ab\n"))
      (insert (propertize "x" 'display '(left-fringe remote-test)) "ab")
      (set-window-start window (point-min))
      (set-window-point window 1))
   '(list :output output
          :bottom (nth 3 (window-inside-pixel-edges window)))))

(ert-deftest remoteterm-tests-fringe-partial-line ()
  "A line visible in part sends only its visible rows of a bitmap."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let* ((rows [1 2 3 4 5 6 7 8 9 10 11 12])
                 ((map :output :bottom)
                  (rd-daemon-eval
                   (remoteterm-tests--partial-fringe-form rows)))
                 (fields (car (remoteterm-tests--fringes output))))
      (should fields)
      (should (= (plist-get fields :y) (- bottom 5)))
      (should (equal (plist-get fields :rows) [1 2 3 4 5]))
      (dolist (message (remoteterm-tests--messages output))
        (when (and (eq (car message) 'clear-area)
                   (< (plist-get (cdr message) :y) bottom))
          (should (<= (+ (plist-get (cdr message) :y)
                         (plist-get (cdr message) :height))
                      bottom)))))))

(defun remoteterm-tests--far-cell-form (parameters columns)
  "Return a form for the daemon that draws at the end of a frame.
PARAMETERS is the list of the parameters of the frame.  The last line
of the window has COLUMNS characters, a bitmap in the right fringe, and
point before the last 2 characters.  The value of the form is a plist
with :output, :geometry, :line, and :right.  :line is the index of the
last line, and :right is the pixel position of the right edge of the
text area."
  (remoteterm-tests--frame-form
   parameters
   `(progn
      (define-fringe-bitmap 'remote-test [#xff] nil 8 'top)
      (dotimes (_ (1- (window-body-height window))) (insert "abc\n"))
      (insert (propertize "x" 'display '(right-fringe remote-test))
              (make-string ,columns ?a))
      (set-window-point window (- (point-max) 2)))
   `(list :output output
          :geometry ,(remoteterm-tests--geometry-form)
          :line (1- (window-body-height window))
          :right (nth 2 (window-inside-pixel-edges window)))))

(defun remoteterm-tests--check-far-cell (parameters columns)
  "Check that a frame with PARAMETERS sends its far pixels without a change.
The last line of the window has COLUMNS characters.  Check the runs of
glyphs, the cursor, and the fringe bitmap of that line, and return
\(X Y), the pixel position of the cursor."
  (pcase-let*
      (((map :output :line :right
             (:geometry (map :x0 :top :ascent :char-width :line-height)))
        (rd-daemon-eval (remoteterm-tests--far-cell-form parameters columns)))
       (line-top (+ top (* line line-height)))
       (runs (remoteterm-tests--line-runs output (+ line-top ascent)))
       (x (+ x0 (* (- columns 2) char-width)))
       (fields (seq-find (lambda (fields) (= (plist-get fields :y) line-top))
                         (remoteterm-tests--fringes output))))
    (should runs)
    (should (= (plist-get (cdr (car runs)) :x) x0))
    (cl-loop for (run next) on runs while next
             do (should (= (plist-get (cdr next) :x)
                           (+ (plist-get (cdr run) :x)
                              (* char-width
                                 (length (plist-get (cdr run) :glyphs)))))))
    (should (equal (seq-take (cdr (remoteterm-tests--last-cursor output)) 2)
                   (list x line-top)))
    (should fields)
    (should (<= right (plist-get fields :x)))
    (list x line-top)))

(ert-deftest remoteterm-tests-large-frame-columns ()
  "The messages of the last columns of a very wide frame keep their x."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (let ((position (remoteterm-tests--check-far-cell '((width . 9000)) 8900)))
      (should (> (car position) #x10000)))))

(ert-deftest remoteterm-tests-large-frame-lines ()
  "The messages of the last line of a very tall frame keep their y."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (let ((position (remoteterm-tests--check-far-cell
                     '((width . 20) (height . 6000)) 10)))
      (should (> (cadr position) #x10000)))))

(ert-deftest remoteterm-tests-large-frame ()
  "A frame wider and taller than 16 bits keeps its pixel values."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let (((map :output :width)
                 (rd-daemon-eval
                  (remoteterm-tests--frame-form
                   '((width . 9000))
                   '(insert "abc")
                   '(progn
                      (with-current-buffer buffer (erase-buffer))
                      (redisplay t)
                      (list :output (remote-display--take-output display)
                            :width (window-body-width window t)))))))
      (should (> width #x10000))
      (should (cl-some (lambda (message)
                         (and (eq (car message) 'clear-area)
                              (> (plist-get (cdr message) :width) #x10000)))
                       (remoteterm-tests--messages output))))))

(ert-deftest remoteterm-tests-clear-area-size ()
  "A clear sends its rectangle if the rectangle has pixels."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-area"
    (remoteterm-tests--with-frame frame "remote-area" nil
      (remote-display--record "remote-area")
      (rd-gen-for-all
       (lambda ()
         (list (rd-gen-int32) (rd-gen-int32) (rd-gen-int32) (rd-gen-int32)))
       (lambda (area)
         (apply #'remote-display--clear-area frame area)
         (pcase-let ((`(,_ ,_ ,width ,height) area)
                     (messages (remoteterm-tests--messages
                                (remote-display--take-output "remote-area"))))
           (should (equal (mapcar (lambda (message)
                                    (list (plist-get (cdr message) :x)
                                          (plist-get (cdr message) :y)
                                          (plist-get (cdr message) :width)
                                          (plist-get (cdr message) :height)))
                                  messages)
                          (and (> width 0) (> height 0) (list area))))))))))

(defun remoteterm-tests--clipped-copy (window current desired height)
  "Return the fields that law 28 gives for a run of WINDOW, or nil.
CURRENT, DESIRED, and HEIGHT are as for `remote-display--scroll-run'.
The value is the list (X Y WIDTH HEIGHT X TO-Y)."
  (pcase-let* ((`(,left ,_ ,right ,_) (window-pixel-edges window))
               (`(,_ ,top ,_ ,bottom) (window-inside-pixel-edges window))
               (distance (- desired current))
               (to-top (max (+ top desired) top))
               (to-bottom (min (+ top desired height) bottom
                               (+ bottom distance))))
    (when (< to-top to-bottom)
      (list left (- to-top distance) (- right left) (- to-bottom to-top)
            left to-top))))

(defun remoteterm-tests--check-scroll-hook (window display)
  "Check law 28 for random runs of WINDOW in the session DISPLAY.
Each value is in or near the text area, or very far from it.  Return
the number of copies."
  (let* ((rows (window-body-height window t))
         (near (lambda () (rd-gen-integer (- (* 2 rows)) (* 2 rows))))
         (far (lambda () (rd-gen-integer (- (ash 1 30)) (ash 1 30))))
         (height (lambda () (rd-gen-element
                             (list (rd-gen-integer 1 (* 2 rows))
                                   (rd-gen-integer 1 (1- (ash 1 31)))))))
         (copies 0))
    (remote-display--take-output display)
    (rd-gen-for-all
     (lambda ()
       (let ((position (rd-gen-element (list near near near far))))
         (list (funcall position) (funcall position) (funcall height))))
     (lambda (run)
       (apply #'remote-display--scroll-run window run)
       (let ((expected (apply #'remoteterm-tests--clipped-copy window run)))
         (when expected
           (cl-incf copies))
         (should (equal
                  (mapcar (lambda (message)
                            (mapcar (lambda (name)
                                      (plist-get (cdr message) name))
                                    '(:x :y :width :height :to-x :to-y)))
                          (remoteterm-tests--messages
                           (remote-display--take-output display)))
                  (and expected (list expected)))))))
    copies))

(ert-deftest remoteterm-tests-scroll-clip ()
  "A scroll sends the part of a run that stays in the text area."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-area"
    (remoteterm-tests--with-frame frame "remote-area" nil
      (let ((window (frame-root-window frame)))
        (remote-display--record "remote-area")
        (should (> (remoteterm-tests--check-scroll-hook window "remote-area")
                   0))))))

(ert-deftest remoteterm-tests-scroll-clip-examples ()
  "Examples of the clip of a scroll, computed by hand."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-area"
    (remoteterm-tests--with-frame frame "remote-area" nil
      (let* ((window (frame-root-window frame))
             (rows (window-body-height window t)))
        (remote-display--record "remote-area")
        (pcase-dolist (`(,run ,expected)
                       `(;; No clip.
                         ((0 10 20) (0 20 10))
                         ;; Off the top: the copy loses its first 5 rows.
                         ((10 -5 20) (15 15 0))
                         ;; Past the bottom at the destination.
                         ((0 10 ,rows) (0 ,(- rows 10) 10))
                         ;; Past the bottom at the source.
                         ((10 0 ,rows) (10 ,(- rows 10) 0))
                         ;; No rows left.
                         ((30 -20 20) nil)
                         ((0 ,rows 20) nil)
                         ;; No rows left, also if the count of the rows
                         ;; does not fit in an int.
                         ((#x7fffffff ,(- 2 (ash 1 31)) #x7fffffff) nil)))
          (apply #'remote-display--scroll-run window run)
          (let ((fields (cdr (assq 'copy-area
                                   (remoteterm-tests--messages
                                    (remote-display--take-output
                                     "remote-area"))))))
            (ert-info ((format "%S" run) :prefix "Run: ")
              (should (equal (and fields
                                  (list (plist-get fields :y)
                                        (plist-get fields :height)
                                        (plist-get fields :to-y)))
                             expected)))))))))

(ert-deftest remoteterm-tests-scroll-position-x ()
  "A scroll in a very wide frame keeps its values beyond 16 bits."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-area"
    (remoteterm-tests--with-frame frame "remote-area" '((width . 20000))
      (let ((window (split-window (frame-root-window frame) 9000 t)))
        (should (> (window-pixel-left window) #x10000))
        (should (> (window-pixel-width window) #x10000))
        (remote-display--record "remote-area")
        (should (> (remoteterm-tests--check-scroll-hook window "remote-area")
                   0))))))

(ert-deftest remoteterm-tests-scroll-position-y ()
  "A scroll in a very tall frame keeps its values beyond 16 bits."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-area"
    (remoteterm-tests--with-frame frame "remote-area"
        '((width . 20) (height . 12000))
      (let ((window (split-window (frame-root-window frame) 5000)))
        (should (> (window-pixel-top window) #x10000))
        (should (> (window-pixel-height window) #x10000))
        (remote-display--record "remote-area")
        (should (> (remoteterm-tests--check-scroll-hook window "remote-area")
                   0))))))

(ert-deftest remoteterm-tests-scroll-no-width ()
  "A scroll of a window that has no pixels of width sends no message."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-area"
    (remoteterm-tests--with-frame frame "remote-area" '((width . 40))
      (let ((window (frame-root-window frame)))
        (split-window window 2 t)
        (set-frame-parameter frame 'right-divider-width
                             (window-pixel-width window))
        (remote-display--record "remote-area")
        (remote-display--scroll-run window 0 10 10)
        (should (equal (remote-display--take-output "remote-area") ""))))))

(ert-deftest remoteterm-tests-empty-cursor ()
  "A cursor with a width or a height of 0 sends no draw-cursor message."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (dolist (type '((bar . 0) (hbar . 0)))
      (pcase-let (((map :from-output :to-output)
                   (rd-daemon-eval
                    (remoteterm-tests--cursor-form "abc" 0 1 type))))
        (should-not (assq 'draw-cursor
                          (remoteterm-tests--messages
                           (concat from-output to-output))))))))

(defun remoteterm-tests--draw-whole-fringe (window width height offset)
  "Call the fringe hook of WINDOW for a bitmap on a line that is all visible.
The bitmap has the size WIDTH, HEIGHT, and starts at row OFFSET of its
data.  It is at the top of the text area."
  (remote-display--draw-fringe window width height offset
                               (nth 1 (window-inside-pixel-edges window))
                               0 (* 4 remote-display--max-fringe-rows)))

(ert-deftest remoteterm-tests-fringe-no-clear-area ()
  "A bitmap with no clear area sends no clear-area message."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-area"
    (remoteterm-tests--with-frame frame "remote-area" nil
      (remote-display--record "remote-area")
      (remoteterm-tests--draw-whole-fringe (frame-root-window frame) 8 1 0)
      (should-not (assq 'clear-area
                        (remoteterm-tests--messages
                         (remote-display--take-output "remote-area")))))))

(ert-deftest remoteterm-tests-fringe-function-limits ()
  "The positions of the fringe test function have half of the int range."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-area"
    (remoteterm-tests--with-frame frame "remote-area" nil
      (let ((window (frame-root-window frame))
            (limit (ash 1 30)))
        (dolist (value (list limit (- (1+ limit))))
          (should-error (remote-display--draw-fringe window 8 1 0 value 0 1)
                        :type 'args-out-of-range)
          (should-error (remote-display--draw-fringe window 8 1 0 0 value 1)
                        :type 'args-out-of-range)
          (should-error (remote-display--draw-fringe window 8 1 0 0 0 value)
                        :type 'args-out-of-range))
        (dolist (value (list (1- limit) (- limit)))
          (remote-display--draw-fringe window 8 1 0 value 0 1))))))

(ert-deftest remoteterm-tests-draw-cursor-function-keeps-state ()
  "The cursor test function does not change the cursor of the window."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let ((`(,before ,after)
                 (rd-daemon-eval
                  (remoteterm-tests--frame-form
                   nil '(insert "abc")
                   '(let ((before (remote-display--cursor-state window)))
                      (remote-display--draw-cursor window 1000)
                      (list before (remote-display--cursor-state window)))))))
      (should before)
      (should (equal before after)))))

(ert-deftest remoteterm-tests-fringe-size ()
  "A fringe bitmap sends a message if the message can hold the bitmap."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-area"
    (remoteterm-tests--with-frame frame "remote-area" nil
      (remote-display--record "remote-area")
      (dolist (width (list (- (ash 1 31)) -1 0 1 8 #xffff #x10000))
        (dolist (height (list -1 0 1 remote-display--max-fringe-rows
                              (1+ remote-display--max-fringe-rows)))
          (dolist (offset (list (- remote-display--max-fringe-rows) -1 0 1
                                remote-display--max-fringe-rows))
            (ert-info ((format "%S" (list width height offset))
                       :prefix "Bitmap: ")
              (remoteterm-tests--draw-whole-fringe (frame-root-window frame)
                                                   width height offset)
              (let ((messages (remoteterm-tests--messages
                               (remote-display--take-output "remote-area")))
                    (rows (- height (max 0 (- offset)))))
                (should (equal
                         (mapcar (lambda (message)
                                   (list (plist-get (cdr message) :width)
                                         (length (plist-get (cdr message)
                                                            :rows))))
                                 messages)
                         (and (<= 1 width #xffff)
                              (<= 1 height)
                              (<= 1 rows remote-display--max-fringe-rows)
                              (list (list width rows)))))))))))))

(ert-deftest remoteterm-tests-fringe-offset-limit ()
  "The fringe test function refuses an offset outside its data."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-area"
    (remoteterm-tests--with-frame frame "remote-area" nil
      (dolist (offset (list (- (1+ remote-display--max-fringe-rows))
                            (1+ remote-display--max-fringe-rows)
                            2000000000))
        (should-error (remoteterm-tests--draw-whole-fringe
                       (frame-root-window frame) 8 1 offset)
                      :type 'args-out-of-range))
      (should-error (remoteterm-tests--draw-whole-fringe
                     (frame-root-window frame) 8
                     (1+ (* 2 remote-display--max-fringe-rows)) 0)
                    :type 'args-out-of-range))))

(defun remoteterm-tests--fringe-case ()
  "Return (HEIGHT OFFSET Y ROW-Y VISIBLE) for the fringe hook.
Y is the distance of the bitmap from the top of the text area."
  (list (rd-gen-integer 0 20) (rd-gen-integer -5 5)
        (rd-gen-integer -30 30) (rd-gen-integer -10 10)
        (rd-gen-integer 0 30)))

(defun remoteterm-tests--expected-fringe (case top)
  "Return (Y . ROWS), the message for CASE with the text area at TOP.
CASE is as for `remoteterm-tests--fringe-case'.  The rows of the data
of the bitmap of the hook are their indexes.  The value is nil if the
bitmap has no visible row.  The visible part of the line goes from the
top of the line, or the top of the text area, for VISIBLE rows.  The
rows before a negative OFFSET have no pixels."
  (pcase-let* ((`(,height ,offset ,y ,row-y ,visible) case)
               (first (max (- offset) 0 (- (max 0 row-y) y)))
               (end (min height (- (+ (max 0 row-y) visible) y))))
    (and (< first end)
         (cons (+ top y first)
               (vconcat (number-sequence (+ offset first)
                                         (+ offset end -1)))))))

(ert-deftest remoteterm-tests-fringe-rows ()
  "A fringe message holds the visible rows of the bitmap, from its offset."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-area"
    (remoteterm-tests--with-frame frame "remote-area" nil
      (remote-display--record "remote-area")
      (let* ((window (frame-root-window frame))
             (top (nth 1 (window-inside-pixel-edges window))))
        (rd-gen-for-all
         #'remoteterm-tests--fringe-case
         (lambda (case)
           (pcase-let ((`(,height ,offset ,y ,row-y ,visible) case))
             (remote-display--draw-fringe window 8 height offset (+ top y)
                                          row-y visible))
           (should (equal (mapcar (lambda (fields)
                                    (cons (plist-get fields :y)
                                          (plist-get fields :rows)))
                                  (remoteterm-tests--fringes
                                   (remote-display--take-output "remote-area")))
                          (let ((expected (remoteterm-tests--expected-fringe
                                           case top)))
                            (and expected (list expected)))))))))))

(ert-deftest remoteterm-tests-test-functions-need-remote-frame ()
  "The test functions signal an error for a frame that is not remote."
  (skip-unless (featurep 'remote-test))
  (let ((frame (selected-frame))
        (window (selected-window)))
    (should-not (eq (framep frame) 'remote))
    (should-error (remote-display--focus frame))
    (should-error (remote-display--clear-area frame 0 0 1 1))
    (should-error (remote-display--scroll-run window 0 1 1))
    (should-error (remote-display--draw-fringe window 8 1 0 0 0 100))
    (should-error (remote-display--draw-cursor window 0))))

(defun remoteterm-tests--frame-id (frame)
  "Return the frame ID of FRAME."
  (string-to-number (frame-parameter frame 'window-id)))

(ert-deftest remoteterm-tests-frame-id-above-int ()
  "A frame ID above the range of a 32-bit int keeps its value."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-id"
    (remote-display--set-last-frame-id "remote-id" #x7fffffff)
    (remoteterm-tests--with-frame frame "remote-id" nil
      (should (= (remoteterm-tests--frame-id frame) #x80000000))
      (remote-display--record "remote-id")
      (remote-display--clear-area frame 0 0 1 1)
      (should (equal (mapcar (lambda (message)
                               (plist-get (cdr message) :frame))
                             (remoteterm-tests--messages
                              (remote-display--take-output "remote-id")))
                     (list #x80000000))))))

(ert-deftest remoteterm-tests-frame-id-counter ()
  "The counter of frame IDs holds each unsigned 32-bit value."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-id"
    (dolist (id '(0 #x7fffffff #x80000000 #xffffffff))
      (should (= (remote-display--set-last-frame-id "remote-id" id) id)))))

(defun remoteterm-tests--ids-after (start count)
  "Return the IDs of COUNT frames made when the counter of IDs is START.
A frame with the ID 1 is live while the session makes these frames."
  (remoteterm-tests--with-frame first "remote-id" nil
    (should (= (remoteterm-tests--frame-id first) 1))
    (remote-display--set-last-frame-id "remote-id" start)
    (let ((frames nil))
      (unwind-protect
          (progn
            (dotimes (_ count)
              (push (remoteterm-tests--make-frame "remote-id" nil) frames))
            (cons 1 (mapcar #'remoteterm-tests--frame-id frames)))
        (mapc #'delete-frame frames)))))

(ert-deftest remoteterm-tests-frame-id-wrap ()
  "The frame IDs of a session stay distinct when the counter wraps."
  (skip-unless (featurep 'remote-test))
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     (lambda ()
       (cons (rd-gen-element (list 0 1 #xfffffffd #xfffffffe #xffffffff
                                   (rd-gen-uint32)))
             (rd-gen-integer 1 4)))
     (lambda (case)
       (remoteterm-tests--with-display "remote-id"
         (let ((ids (remoteterm-tests--ids-after (car case) (cdr case))))
           (should (cl-every (lambda (id) (<= 1 id #xffffffff)) ids))
           (should (= (length (delete-dups (copy-sequence ids)))
                      (length ids)))))))))

(ert-deftest remoteterm-tests-frame-ids-per-session ()
  "A session counts its own frames, and the frames of others change nothing."
  (skip-unless (featurep 'remote-test))
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     (lambda ()
       (rd-gen-list (lambda () (rd-gen-element '("remote-a" "remote-b"))) 6))
     (lambda (displays)
       (remoteterm-tests--with-display "remote-a"
         (remoteterm-tests--with-display "remote-b"
           (let ((frames nil)
                 (counts (make-hash-table :test #'equal)))
             (unwind-protect
                 (dolist (display displays)
                   (let ((frame (remoteterm-tests--make-frame display nil)))
                     (push frame frames)
                     (should (= (remoteterm-tests--frame-id frame)
                                (cl-incf (gethash display counts 0))))))
               (mapc #'delete-frame frames)))))))))

;;;; Draw colors

(defun remoteterm-tests--rgb-string (rgb)
  "Return the color string of RGB, a list of three 8-bit components."
  (apply #'format "#%02x%02x%02x" rgb))

(defun remoteterm-tests--rgb-pixel (rgb)
  "Return the protocol color #xRRGGBB of RGB."
  (+ (ash (nth 0 rgb) 16) (ash (nth 1 rgb) 8) (nth 2 rgb)))

(defun remoteterm-tests--color-list (count)
  "Return a list of COUNT random colors, where colors are often equal.
A color that is equal to an earlier color finds a draw that takes the
wrong one of two colors."
  (let ((colors nil))
    (dotimes (_ count (nreverse colors))
      (push (if (and colors (zerop (rd-gen-random 3)))
                (rd-gen-element colors)
              (remoteterm-tests--rgb))
            colors))))

(defun remoteterm-tests--colors-case ()
  "Return random arguments for `remoteterm-tests--face-colors-form'."
  (cons (make-string (rd-gen-integer 1 20) ?x)
        (remoteterm-tests--color-list 4)))

(defun remoteterm-tests--face-colors-form (text fg bg frame-fg frame-bg)
  "Return a form for the daemon that draws TEXT with a face, then a \"z\".
TEXT has the foreground FG and the background BG, and is on the first
line.  The \"z\" is in the default face on the second line, and point
is after it.  FRAME-FG and FRAME-BG are the colors of the frame.  Each
color is a list of three 8-bit components.  The value of the form is a
plist.  :output is the bytes that the session sent, and :geometry is
as for `remoteterm-tests--geometry-form'."
  (remoteterm-tests--frame-form
   `((width . 60)
     (foreground-color . ,(remoteterm-tests--rgb-string frame-fg))
     (background-color . ,(remoteterm-tests--rgb-string frame-bg)))
   `(progn
      (insert (propertize ,text 'face
                          '(:foreground ,(remoteterm-tests--rgb-string fg)
                            :background ,(remoteterm-tests--rgb-string bg)))
              "\nz")
      (set-window-point window (point-max)))
   `(list :output output :geometry ,(remoteterm-tests--geometry-form))))

(defun remoteterm-tests--of-type (type bytes)
  "Return the fields of the messages of TYPE in BYTES."
  (mapcar #'cdr (seq-filter (lambda (message) (eq (car message) type))
                            (remoteterm-tests--messages bytes))))

(defun remoteterm-tests--colors (bytes)
  "Return all color values of the messages in BYTES."
  (cl-loop for (_type . fields) in (remoteterm-tests--messages bytes)
           append (cl-loop for key in '(:color :foreground :background)
                           when (plist-get fields key) collect it)))

(rd-daemon-deftest remoteterm-tests-face-colors
  "The glyphs and the clears of a face have the colors of the face."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     #'remoteterm-tests--colors-case
     (lambda (case)
       (pcase-let*
           ((`(,text ,fg ,bg ,frame-fg ,frame-bg) case)
            ((map :output (:geometry (map :x0 :top :ascent :char-width
                                          :line-height)))
             (rd-daemon-eval
              (apply #'remoteterm-tests--face-colors-form case)))
            (text-end (+ x0 (* (length text) char-width)))
            (glyphs (remoteterm-tests--of-type 'draw-glyphs output))
            (clears (seq-filter (lambda (fields)
                                  (= (plist-get fields :y) top))
                                (remoteterm-tests--of-type 'clear-area
                                                           output))))
         (should (cl-every (lambda (color) (< color #x1000000))
                           (remoteterm-tests--colors output)))
         (cl-flet ((text-p (fields)
                     (and (= (plist-get fields :y) (+ top ascent))
                          (< (plist-get fields :x) text-end)))
                   (z-p (fields)
                     (and (= (plist-get fields :y) (+ top line-height ascent))
                          (= (plist-get fields :x) x0))))
           (should (seq-some #'text-p glyphs))
           (should (seq-some #'z-p glyphs))
           (dolist (fields glyphs)
             (cond
              ((text-p fields)
               (should (= (plist-get fields :color)
                          (remoteterm-tests--rgb-pixel fg))))
              ((z-p fields)
               (should (= (plist-get fields :color)
                          (remoteterm-tests--rgb-pixel frame-fg)))))))
         (should (seq-some (lambda (fields)
                             (and (< (plist-get fields :x) text-end)
                                  (= (plist-get fields :color)
                                     (remoteterm-tests--rgb-pixel bg))))
                           clears))
         ;; The text has at most 20 columns, and the text area has more
         ;; than 21.  The right fringe has the colors of its face.
         (dolist (fields clears)
           (when (<= text-end (plist-get fields :x) (+ x0 (* 21 char-width)))
             (should (= (plist-get fields :color)
                        (remoteterm-tests--rgb-pixel frame-bg))))))))))

(defun remoteterm-tests--cursor-colors-case ()
  "Return random arguments for `remoteterm-tests--cursor-colors-form'.
The cursor color is often a color of the frame or of the face.  The
glyph under the cursor has the default face, or a face with random
colors."
  (pcase-let* ((`(,frame-fg ,frame-bg ,cursor)
                (remoteterm-tests--color-list 3))
               (face-bg (rd-gen-element
                         (list cursor frame-bg (remoteterm-tests--rgb))))
               (face-fg (rd-gen-element
                         (list cursor face-bg (remoteterm-tests--rgb)))))
    (list (remoteterm-tests--cursor-type) frame-fg frame-bg cursor
          (and (/= (rd-gen-random 4) 0)
               (list :foreground (remoteterm-tests--rgb-string face-fg)
                     :background (remoteterm-tests--rgb-string face-bg))))))

(defun remoteterm-tests--expected-cursor-colors (type frame-fg frame-bg cursor
                                                      face)
  "Return the colors of a cursor, as `pgtk_set_cursor_gc' gives them.
TYPE, FRAME-FG, FRAME-BG, CURSOR, and FACE are as for
`remoteterm-tests--cursor-colors-form'.  The value is the list of the
arguments BOX-COLOR, GLYPH-COLOR, and LINE-COLOR of
`remoteterm-tests--check-cursor-colors'."
  (let* ((pixel (lambda (color)
                  (if (stringp color)
                      (string-to-number (substring color 1) 16)
                    (remoteterm-tests--rgb-pixel color))))
         (color (funcall pixel (if (equal cursor frame-bg) frame-fg cursor)))
         (face-fg (funcall pixel (or (plist-get face :foreground) frame-fg)))
         (frame-bg (funcall pixel frame-bg))
         (face-bg (if face (funcall pixel (plist-get face :background))
                    frame-bg))
         (glyph (cond ((/= face-bg color) face-bg)
                      ((/= face-fg color) face-fg)
                      (t frame-bg)))
         (line (if (= face-bg color) face-fg color)))
    (cond ((not (eq type 'box)) (list color nil line))
          ;; A box with the colors of the face is not visible, so it has
          ;; the reverse video of the face.
          ((and (= color face-bg) (= glyph face-fg))
           (list face-fg face-bg line))
          (t (list color glyph line)))))

(defun remoteterm-tests--cursor-colors-form (type frame-fg frame-bg cursor
                                                 &optional face)
  "Return a form for the daemon that draws a cursor of TYPE on a glyph.
The text is \"abc\", and the cursor is on \"b\".  FRAME-FG, FRAME-BG,
and CURSOR are the colors of the frame and of its cursor, each a list
of three 8-bit components.  FACE is nil or the face of the \"b\".  The
value of the form is a plist.  :output is the bytes that the session
sent, :code is the glyph code of \"b\", and :geometry is as for
`remoteterm-tests--geometry-form'."
  (remoteterm-tests--frame-form
   `((width . 40)
     (foreground-color . ,(remoteterm-tests--rgb-string frame-fg))
     (background-color . ,(remoteterm-tests--rgb-string frame-bg))
     (cursor-color . ,(remoteterm-tests--rgb-string cursor)))
   `(progn
      (insert "a" (propertize "b" 'face ',face) "c")
      (setq cursor-type ',type)
      (set-window-point window 2))
   `(list :output output
          :code (aref ,(remoteterm-tests--codes-form "b") 0)
          :geometry ,(remoteterm-tests--geometry-form))))

(defun remoteterm-tests--after-cursor (bytes)
  "Return the messages in BYTES after the last draw-cursor message."
  (let ((after nil))
    (dolist (message (remoteterm-tests--messages bytes) (nreverse after))
      (if (eq (car message) 'draw-cursor)
          (setq after nil)
        (push message after)))))

(defun remoteterm-tests--check-cursor-colors (output code geometry
                                                     box-color glyph-color
                                                     &optional line-color)
  "Check the colors of the cursor in OUTPUT.
The last draw-cursor message has BOX-COLOR.  After a box, the glyph
CODE at the cursor is drawn again in GLYPH-COLOR, and no clear-area
message covers a part of the box.  GEOMETRY is as for
`remoteterm-tests--geometry-form'.  A bar or an hbar has LINE-COLOR,
with a default of BOX-COLOR."
  (pcase-let* (((map :x0 :char-width) geometry)
               (cursor (car (last (remoteterm-tests--of-type 'draw-cursor
                                                             output))))
               (after (remoteterm-tests--after-cursor output)))
    (should cursor)
    (if (memq (plist-get cursor :kind) '(2 3))
        (should (= (plist-get cursor :color) (or line-color box-color)))
      (should (= (plist-get cursor :color) box-color)))
    (when (= (plist-get cursor :kind) 0)
      (should-not (seq-some (lambda (message)
                              (and (eq (car message) 'clear-area)
                                   (remoteterm-tests--overlap-p
                                    (cdr message) cursor)))
                            after))
      (should (seq-some (lambda (message)
                          (and (eq (car message) 'draw-glyphs)
                               (= (plist-get (cdr message) :x)
                                  (+ x0 char-width))
                               (equal (plist-get (cdr message) :glyphs)
                                      (vector code))
                               (= (plist-get (cdr message) :color)
                                  glyph-color)))
                        after)))))

(defun remoteterm-tests--overlap-p (a b)
  "Return non-nil if the rectangles of the fields A and B have a common pixel."
  (cl-flet ((inside (start size other-start other-size)
              (and (< start (+ other-start other-size))
                   (< other-start (+ start size)))))
    (and (inside (plist-get a :x) (plist-get a :width)
                 (plist-get b :x) (plist-get b :width))
         (inside (plist-get a :y) (plist-get a :height)
                 (plist-get b :y) (plist-get b :height)))))

(rd-daemon-deftest remoteterm-tests-cursor-colors
  "A cursor has the cursor color, and a box shows the glyph under it."
  (let ((rd-gen-trials 40))
    (rd-gen-for-all
     #'remoteterm-tests--cursor-colors-case
     (lambda (case)
       (pcase-let (((map :output :code :geometry)
                    (rd-daemon-eval
                     (apply #'remoteterm-tests--cursor-colors-form case))))
         (apply #'remoteterm-tests--check-cursor-colors output code geometry
                (apply #'remoteterm-tests--expected-cursor-colors case)))))))

(defconst remoteterm-tests--cursor-on-face-cases
  '(;; A box swaps the colors, so it differs from the text.
    ((:foreground "#405060" :background "#102030") #x405060 #x102030 #x405060)
    ;; The face has only the cursor color, so the glyph has the
    ;; background of the frame.
    ((:foreground "#102030" :background "#102030") #x102030 #xffffff #x102030)
    ;; The face does not have the cursor color.
    ((:foreground "#405060" :background "#708090") #x102030 #x708090 #x102030))
  "Cases of a cursor with the color #x102030 on a face, on a white frame.
Each case is (FACE BOX-COLOR GLYPH-COLOR LINE-COLOR), as for
`remoteterm-tests--check-cursor-colors'.")

(ert-deftest remoteterm-tests-cursor-colors-on-face ()
  "A cursor on a face has the colors that `pgtk_set_cursor_gc' gives."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-dolist (`(,face . ,colors) remoteterm-tests--cursor-on-face-cases)
      (dolist (type '(box (bar . 2) (hbar . 3)))
        (ert-info ((format "%S %S" face type) :prefix "Case: ")
          (pcase-let (((map :output :code :geometry)
                       (rd-daemon-eval
                        (remoteterm-tests--cursor-colors-form
                         type '(0 0 0) '(255 255 255) '(16 32 48) face))))
            (apply #'remoteterm-tests--check-cursor-colors
                   output code geometry colors)))))))

;; The bitmaps of a fringe colors test.  They have different shapes.
(defconst remoteterm-tests--color-bitmaps
  '(right-arrow left-arrow up-arrow down-arrow right-triangle))

(defun remoteterm-tests--fringe-colors-case ()
  "Return random arguments for `remoteterm-tests--fringe-colors-form'.
The two colors are often equal."
  (append (list (rd-gen-element '(left-fringe right-fringe))
                (rd-gen-element remoteterm-tests--color-bitmaps))
          (remoteterm-tests--color-list 2)))

(defun remoteterm-tests--fringe-colors-form (side bitmap fg bg)
  "Return a form for the daemon that draws BITMAP in a fringe, and removes it.
SIDE is `left-fringe' or `right-fringe'.  The `fringe' face of the
frame has the foreground FG and the background BG, each a list of three
8-bit components.  The value of the form is a plist.  :draw-output and
:remove-output are the bytes of each draw.  :fringe-x is the left edge
of the fringe of SIDE, and :fringe-width is its width.  :top is as for
`remoteterm-tests--geometry-form'."
  (remoteterm-tests--frame-form
   '((width . 40))
   `(progn
      (set-face-attribute 'fringe frame
                          :foreground ,(remoteterm-tests--rgb-string fg)
                          :background ,(remoteterm-tests--rgb-string bg))
      (insert (propertize "x" 'display '(,side ,bitmap)) "abc\n"))
   `(progn
      (with-current-buffer buffer
        (remove-text-properties 1 2 '(display nil)))
      (redisplay t)
      (let ((edges (window-inside-pixel-edges window)))
        (list :draw-output output
              :remove-output (remote-display--take-output display)
              :top (nth 1 edges)
              :fringe-width ,(if (eq side 'left-fringe)
                                 '(car (window-fringes window))
                               '(cadr (window-fringes window)))
              :fringe-x ,(if (eq side 'left-fringe)
                             '(- (nth 0 edges) (car (window-fringes window)))
                           '(nth 2 edges)))))))

(rd-daemon-deftest remoteterm-tests-fringe-colors
  "A bitmap and the clear of its area have the colors of the fringe face."
  (let ((rd-gen-trials 10))
    (rd-gen-for-all
     #'remoteterm-tests--fringe-colors-case
     (lambda (case)
       (pcase-let* ((`(,_side ,_bitmap ,fg ,bg) case)
                    ((map :draw-output :remove-output :top :fringe-x
                          :fringe-width)
                     (rd-daemon-eval
                      (apply #'remoteterm-tests--fringe-colors-form case)))
                    ;; The bitmap is at the center of the fringe and of
                    ;; the line, so find it by its fringe.
                    (bitmap (seq-find (lambda (fields)
                                        (<= fringe-x (plist-get fields :x)
                                            (+ fringe-x fringe-width -1)))
                                      (remoteterm-tests--fringes
                                       draw-output))))
         (should bitmap)
         (should (= (plist-get bitmap :foreground)
                    (remoteterm-tests--rgb-pixel fg)))
         (should (= (plist-get bitmap :background)
                    (remoteterm-tests--rgb-pixel bg)))
         (should (seq-some (lambda (fields)
                             (and (= (plist-get fields :x) fringe-x)
                                  (= (plist-get fields :y) top)
                                  (= (plist-get fields :color)
                                     (remoteterm-tests--rgb-pixel bg))))
                           (remoteterm-tests--of-type 'clear-area
                                                      remove-output))))))))

(rd-daemon-deftest remoteterm-tests-fringe-cursor-color
  "A cursor in the fringe has the cursor color."
  (let ((rd-gen-trials 4))
    (rd-gen-for-all
     (lambda () (remoteterm-tests--color-list 3))
     (lambda (colors)
       (pcase-let* ((`(,fg ,bg ,cursor) colors)
                    ((map :first-output)
                     (rd-daemon-eval
                      (remoteterm-tests--full-line-form
                       'left-to-right '(point-max) nil
                       `(modify-frame-parameters
                         frame
                         '((foreground-color
                            . ,(remoteterm-tests--rgb-string fg))
                           (background-color
                            . ,(remoteterm-tests--rgb-string bg))
                           (cursor-color
                            . ,(remoteterm-tests--rgb-string cursor)))))))
                    (fields (remoteterm-tests--fringe-cursor first-output)))
         (should fields)
         (should (= (plist-get fields :foreground)
                    (remoteterm-tests--rgb-pixel
                     (if (equal cursor bg) fg cursor)))))))))

(rd-daemon-deftest remoteterm-tests-fringe-overlay-cursor-color
  "A bitmap on a box cursor in the fringe has the background of the face."
  (let ((rd-gen-trials 3))
    (rd-gen-for-all
     #'remoteterm-tests--rgb
     (lambda (bg)
       (pcase-let* (((map :first-output)
                     (rd-daemon-eval
                      (remoteterm-tests--full-line-form
                       'left-to-right '(point-max) nil
                       `(progn
                          (setq indicate-buffer-boundaries 'right)
                          (set-face-attribute
                           'fringe frame
                           :background ,(remoteterm-tests--rgb-string bg))))))
                    (fields (seq-find (lambda (fields)
                                        (and (= (plist-get fields :cursor) 1)
                                             (= (plist-get fields :overlay) 1)))
                                      (remoteterm-tests--fringes
                                       first-output))))
         (should fields)
         (should (= (plist-get fields :foreground)
                    (remoteterm-tests--rgb-pixel bg))))))))

;;;; Fonts

(declare-function remote-display--file-hash "remotetest.c" (file))

(defun remoteterm-tests--sha256 (file)
  "Return the SHA-256 hash of the bytes of FILE, as a vector of bytes."
  (with-temp-buffer
    (set-buffer-multibyte nil)
    (insert-file-contents-literally file)
    (vconcat (secure-hash 'sha256 (current-buffer) nil nil t))))

(ert-deftest remoteterm-tests-file-hash ()
  "The hash of a file is the SHA-256 hash of its bytes."
  (skip-unless (featurep 'remote-test))
  (let ((file (make-temp-file "rd-hash")))
    (unwind-protect
        (rd-gen-for-all
         ;; The lengths at the block limits of SHA-256, and a length
         ;; above the size of the read buffer.
         (lambda ()
           (apply #'unibyte-string
                  (cl-loop repeat (rd-gen-element
                                   '(0 1 55 56 63 64 65 119 120 70000))
                           collect (rd-gen-random 256))))
         (lambda (bytes)
           (let ((coding-system-for-write 'no-conversion))
             (write-region bytes nil file nil 'silent))
           (should (equal (remote-display--file-hash file)
                          (remoteterm-tests--sha256 file)))))
      (delete-file file))
    ;; A file that does not exist, a file that cannot be read, and a
    ;; file name that is not a string have no hash.
    (should (equal (remote-display--file-hash file) []))
    (should (equal (remote-display--file-hash temporary-file-directory) []))
    (should (equal (remote-display--file-hash nil) []))))

(declare-function remote-display--set-font-file "remotetest.c" (font file))

(defvar remoteterm-tests--last-font-size 40
  "The last pixel size of `remoteterm-tests--font-file-identity'.
Each trial uses a new size, so Emacs has not drawn with its fonts.")

(defun remoteterm-tests--distinct-fonts (count frame size again)
  "Return COUNT new font objects of FRAME, each from a different file.
Each font has the characters \"abc\", and the pixel size SIZE.  If
AGAIN is non-nil, find each font through a new font spec, so Emacs
makes a new font object for it.
Return fewer fonts if FRAME has fewer font files."
  (let ((fonts nil)
        (files nil))
    (cl-loop for entity in (list-fonts (font-spec) frame)
             until (= (length fonts) count)
             do (let* ((font (open-font (if again
                                            (remoteterm-tests--new-entity
                                             entity frame)
                                          entity)
                                        size frame))
                       (file (and font (aref (font-info font frame) 12))))
                  (when (and file (not (member file files))
                             (cl-every (lambda (char)
                                         (font-has-char-p font char frame))
                                       "abc"))
                    (push file files)
                    (push font fonts))))
    (nreverse fonts)))

(defun remoteterm-tests--new-entity (entity frame)
  "Return a font entity of FRAME for the font of ENTITY, from a new spec.
The new entity has no font objects yet."
  (car (list-fonts (font-spec :family (font-get entity :family)
                              :weight (font-get entity :weight)
                              :slant (font-get entity :slant)
                              :width (font-get entity :width))
                   frame)))

(defun remoteterm-tests--insert-font-lines (files frame size again)
  "Insert a line for each of FILES, each with its own font of FRAME.
SIZE and AGAIN are as for `remoteterm-tests--distinct-fonts'.  Before
the first draw, the file name of each font is changed to the element
of FILES for its line."
  (let ((fonts (remoteterm-tests--distinct-fonts (length files) frame size
                                                 again)))
    (should (= (length fonts) (length files)))
    (cl-mapc (lambda (font file)
               (remote-display--set-font-file font file)
               (insert (propertize "abc" 'face (list :font font)) "\n"))
             fonts files)))

(defun remoteterm-tests--font-files-form (files size)
  "Return a form for the daemon that draws a line for each of FILES, twice.
Each line has its own font of the pixel size SIZE, and the file name
of that font is the element of FILES, as for
`remoteterm-tests--insert-font-lines'.  The second draw has new font
objects for the fonts.  The value of the form is a plist.  :output and
:again are the bytes of each draw, and :x0 is the left edge of the
text area."
  (remoteterm-tests--frame-form
   '((width . 30))
   `(remoteterm-tests--insert-font-lines ',files frame ,size nil)
   `(list :output output
          :again (with-current-buffer buffer
                   (erase-buffer)
                   (remoteterm-tests--insert-font-lines ',files frame ,size t)
                   (redraw-frame frame)
                   (redisplay t)
                   (remote-display--take-output display))
          :x0 (nth 0 (window-inside-pixel-edges window)))))

(defun remoteterm-tests--line-font-ids (bytes x0 count)
  "Return the font IDs of the first COUNT lines that BYTES draws.
The ID of a line is the font of its draw-glyphs message at X0."
  (seq-take (mapcar #'cdr
                    (seq-uniq (sort (mapcar (lambda (fields)
                                              (cons (plist-get fields :y)
                                                    (plist-get fields :font)))
                                            (seq-filter
                                             (lambda (fields)
                                               (= (plist-get fields :x) x0))
                                             (remoteterm-tests--of-type
                                              'draw-glyphs bytes)))
                                    :key #'car)
                              (lambda (a b) (= (car a) (car b)))))
            count))

(defun remoteterm-tests--index-and-size (fields)
  "Return the list of the face index and the pixel size in FIELDS.
A font that has only some sizes can have a size that is not the size
that Emacs asked for."
  (list (plist-get fields :index) (plist-get fields :size)))

(defconst remoteterm-tests--font-file-keys
  '(missing-a missing-b a a-copy b)
  "The files of a font identity case.
A missing file does not exist.  The files a and a-copy have the same
bytes, and the file b has different bytes.")

(defun remoteterm-tests--font-file-class (key)
  "Return the class of the font file KEY.
Two fonts of the same index and size have the same ID if and only if
their files have the same class."
  (if (eq key 'a-copy) 'a key))

(rd-daemon-deftest remoteterm-tests-font-file-identity
  "Two fonts have one ID if and only if their files have the same bytes.
A font whose file cannot be read has the ID of its file name, and it
keeps that ID after Emacs opens it again."
  (let ((rd-gen-trials 8))
    (rd-gen-for-all
     (lambda ()
       (cl-loop repeat (rd-gen-integer 2 4)
                collect (rd-gen-element remoteterm-tests--font-file-keys)))
     (lambda (keys)
       (let* ((bytes (apply #'unibyte-string
                            (cl-loop repeat (rd-gen-integer 1 100)
                                     collect (rd-gen-random 256))))
              (paths `((missing-a . "/nonexistent/a.ttf")
                       (missing-b . "/nonexistent/b.ttf")
                       (a . ,(make-temp-file "rd-font" nil nil bytes))
                       (a-copy . ,(make-temp-file "rd-font" nil nil bytes))
                       (b . ,(make-temp-file "rd-font" nil nil
                                             (concat bytes "b"))))))
         (unwind-protect
             (pcase-let*
                 (((map :output :again :x0)
                   (rd-daemon-eval
                    (remoteterm-tests--font-files-form
                     (mapcar (lambda (key) (alist-get key paths)) keys)
                     (cl-incf remoteterm-tests--last-font-size))))
                  (definitions (remoteterm-tests--check-font-definitions
                                (remoteterm-tests--messages output)))
                  (ids (remoteterm-tests--line-font-ids output x0
                                                        (length keys))))
               (should (= (length ids) (length keys)))
               (dotimes (i (length keys))
                 (cl-loop
                  for j from (1+ i) below (length keys)
                  do (let ((id (nth i ids))
                           (other-id (nth j ids)))
                       (should
                        (eq (= id other-id)
                            (and (eq (remoteterm-tests--font-file-class
                                      (nth i keys))
                                     (remoteterm-tests--font-file-class
                                      (nth j keys)))
                                 (equal (remoteterm-tests--index-and-size
                                         (alist-get id definitions))
                                        (remoteterm-tests--index-and-size
                                         (alist-get other-id
                                                    definitions)))))))))
               (should-not (remoteterm-tests--of-type 'define-font again))
               (should (equal (remoteterm-tests--line-font-ids again x0
                                                               (length keys))
                              ids)))
           (dolist (path '(a a-copy b))
             (delete-file (alist-get path paths)))))))))

(defun remoteterm-tests--check-font-definitions (messages)
  "Check laws 35 and 36 for MESSAGES, which start with an attach.
Return the alist of the define-font fields of each font ID."
  (let ((defined nil))
    (dolist (message messages (nreverse defined))
      (let ((id (plist-get (cdr message) :font)))
        (pcase (car message)
          ('define-font
           (should-not (assq id defined))
           ;; A file that the server can read gives one ID to a font.
           (unless (equal (plist-get (cdr message) :hash) [])
             (should-not (rassoc (remoteterm-tests--font-identity
                                  (cdr message))
                                 (mapcar (lambda (entry)
                                           (cons (car entry)
                                                 (remoteterm-tests--font-identity
                                                  (cdr entry))))
                                         defined))))
           (push (cons id (cdr message)) defined))
          ('draw-glyphs
           (should (assq id defined))))))))

(defun remoteterm-tests--font-identity (fields)
  "Return the list of the hash, the index, and the size in FIELDS."
  (list (plist-get fields :hash) (plist-get fields :index)
        (plist-get fields :size)))

(defun remoteterm-tests--font-identity-form ()
  "Return a form for the daemon that draws text in two sizes, twice.
The first line has the default font, and the second line has a font
twice as large.  The session draws once, and draws all again after a
new attach.  The value of the form is a plist.  :output and :again are
the bytes of each draw.  :fonts is a list for the two fonts.  Each
element is (FILE SIZE Y), where Y is the baseline of the line of the
font."
  (remoteterm-tests--frame-form
   '((width . 30))
   '(insert "abc\n" (propertize "big" 'face '(:height 2.0)))
   '(let ((again (progn (remote-display--record display)
                        (redraw-frame frame)
                        (redisplay t)
                        (remote-display--take-output display))))
      (list :output output
            :again again
            :fonts
            (mapcar (lambda (position)
                      (let* ((font (font-at position window))
                             (info (font-info font))
                             (glyph-y (with-current-buffer buffer
                                        (save-excursion
                                          (goto-char position)
                                          (cdr (posn-x-y
                                                (posn-at-point position
                                                               window)))))))
                        (list (aref info 12) (aref info 2)
                              (+ (nth 1 (window-inside-pixel-edges window))
                                 glyph-y (aref (query-font font) 4)))))
                    '(1 5))))))

(ert-deftest remoteterm-tests-font-identity ()
  "Each draw names a font that a definition before it gave."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let* (((map :output :again :fonts)
                  (rd-daemon-eval (remoteterm-tests--font-identity-form)))
                 (definitions (remoteterm-tests--check-font-definitions
                               (remoteterm-tests--messages output)))
                 (definitions-again (remoteterm-tests--check-font-definitions
                                     (remoteterm-tests--messages again)))
                 (glyphs (remoteterm-tests--of-type 'draw-glyphs output)))
      (pcase-dolist (`(,file ,size ,y) fonts)
        (ert-info (file :prefix "Font file: ")
          (let ((hash (remoteterm-tests--sha256 file))
                (id (plist-get (seq-find (lambda (fields)
                                           (= (plist-get fields :y) y))
                                         glyphs)
                               :font)))
            (should id)
            (should (equal (plist-get (cdr (assq id definitions)) :hash)
                           hash))
            (should (= (plist-get (cdr (assq id definitions)) :size) size))
            (should (= (plist-get (cdr (assq id definitions)) :file-size)
                       (file-attribute-size (file-attributes file))))
            (unless (string-match-p "\\.ttc\\'" file)
              (should (= (plist-get (cdr (assq id definitions)) :index) 0)))
            (should (equal (assq id definitions-again)
                           (assq id definitions))))))
      (should (/= (nth 1 (car fonts)) (nth 1 (cadr fonts)))))))

(ert-deftest remoteterm-tests-font-face-index ()
  "A named instance of a variable font has its face index in the file."
  (skip-unless (featurep 'remote-test))
  (skip-unless (executable-find "fc-query"))
  (rd-with-daemon
    (pcase-let*
        (((map :output :file)
          (rd-daemon-eval
           (remoteterm-tests--frame-form
            '((width . 30))
            '(insert (propertize "x" 'face '(:family "Ubuntu Sans"
                                             :weight bold)))
            '(list :output output :file (aref (font-info font) 12)))))
         (_ (skip-unless (string-match-p "Ubuntu" file)))
         (indexes (mapcar #'string-to-number
                          (process-lines "fc-query" "-f" "%{index}\n" file)))
         (index (plist-get
                 (seq-find (lambda (fields)
                             (equal (plist-get fields :hash)
                                    (remoteterm-tests--sha256 file)))
                           (remoteterm-tests--of-type 'define-font output))
                 :index)))
      (should index)
      (should (> index 0))
      (should (memq index indexes)))))

(defun remoteterm-tests--faces-case ()
  "Return a random list of 1 to 4 faces, each a list of face attributes."
  (cl-loop repeat (rd-gen-integer 1 4)
           collect (append
                    (list :height (rd-gen-element '(0.8 1.0 1.5 2.0)))
                    (let ((family (rd-gen-element
                                   '(nil "Monospace" "Sans" "Serif"))))
                      (and family (list :family family))))))

(defun remoteterm-tests--reopened-fonts-form (faces)
  "Return a form for the daemon that draws a line in each of FACES, three times.
The session draws once, then again after a new attach, and then again
after Emacs opens its fonts again.  The value of the form is a plist.
:output, :again, and :reopened are the bytes of each draw."
  (remoteterm-tests--frame-form
   '((width . 30))
   `(dolist (face ',faces)
      (insert (propertize "abc" 'face face) "\n"))
   '(cl-flet ((draw ()
                (redraw-frame frame)
                (redisplay t)
                (remote-display--take-output display)))
      (list :output output
            :again (progn (remote-display--record display) (draw))
            :reopened (progn (clear-font-cache) (draw))))))

(defun remoteterm-tests--line-fonts (bytes)
  "Return the sorted list of (Y . FONT) of the draw-glyphs messages in BYTES."
  (sort (seq-uniq (mapcar (lambda (fields)
                            (cons (plist-get fields :y)
                                  (plist-get fields :font)))
                          (remoteterm-tests--of-type 'draw-glyphs bytes)))
        :lessp (lambda (a b) (or (< (car a) (car b))
                                 (and (= (car a) (car b))
                                      (< (cdr a) (cdr b)))))))

(defun remoteterm-tests--sorted-definitions (definitions)
  "Return DEFINITIONS, an alist with font IDs as keys, sorted by ID."
  (sort (copy-sequence definitions) :key #'car))

(rd-daemon-deftest remoteterm-tests-font-kept
  "A font keeps its ID after a new attach, and after Emacs opens it again."
  (let ((rd-gen-trials 10))
    (rd-gen-for-all
     #'remoteterm-tests--faces-case
     (lambda (faces)
       (pcase-let* (((map :output :again :reopened)
                     (rd-daemon-eval
                      (remoteterm-tests--reopened-fonts-form faces)))
                    (definitions (remoteterm-tests--check-font-definitions
                                  (remoteterm-tests--messages output))))
         (should (equal (remoteterm-tests--sorted-definitions
                         (remoteterm-tests--check-font-definitions
                          (remoteterm-tests--messages again)))
                        (remoteterm-tests--sorted-definitions definitions)))
         (should-not (remoteterm-tests--of-type 'define-font reopened))
         (should (remoteterm-tests--of-type 'draw-glyphs reopened))
         (should (equal (remoteterm-tests--line-fonts again)
                        (remoteterm-tests--line-fonts output)))
         (should (equal (remoteterm-tests--line-fonts reopened)
                        (remoteterm-tests--line-fonts output))))))))

(ert-deftest remoteterm-tests-fonts-of-frames ()
  "The frames of a session have the same definitions of their fonts."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (let ((definitions
           (mapcar (lambda (frames)
                     (remoteterm-tests--sorted-definitions
                      (remoteterm-tests--check-font-definitions
                       (remoteterm-tests--messages
                        (plist-get
                         (rd-daemon-eval
                          (remoteterm-tests--frame-of-text-form
                           (remoteterm-tests--distinct-texts frames)))
                         :output)))))
                   '(1 3))))
      (should (car definitions))
      (should (equal (car definitions) (cadr definitions))))))

;;;; Transport

(rd-daemon-deftest remoteterm-tests-process-sink
  "A network process gets the same bytes as a recording client."
  (let ((rd-gen-trials 6))
    (rd-gen-for-all
     #'remoteterm-tests--process-sink-case
     (lambda (case)
       (pcase-let (((map :recorded :received)
                    (rd-daemon-eval
                     (apply #'remoteterm-tests--process-sink-form case))))
         (should (> (length recorded) 0))
         (should (equal received recorded)))))))

;; The messages of the errors of `remote-display-attach'.
(defconst remoteterm-tests--attach-errors
  '((closed . "The process is not a live network connection")
    (attached . "The process is already the client of a session")))

(ert-deftest remoteterm-tests-attach-needs-connection ()
  "A session attaches only a network connection, and not a server."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (rd-daemon-eval (remoteterm-tests--bad-attach-form))
                   (list '(wrong-type-argument processp process)
                         (cons 'error (alist-get 'closed
                                                 remoteterm-tests--attach-errors))
                         (cons 'error (alist-get 'closed
                                                 remoteterm-tests--attach-errors)))))))

(defun remoteterm-tests--bad-attach-form ()
  "Return a form for the daemon that attaches three bad clients.
The clients are a symbol, a pipe process, and a server.  The value of
the form is the list of the errors."
  `(cl-flet ((attach (process)
               (condition-case err
                   (progn (remote-display-attach "remote-attach" process) nil)
                 (error (if (stringp (cadr err))
                            (cons (car err) (cadr err))
                          err)))))
     (x-open-connection "remote-attach")
     (unwind-protect
         (list (attach 'process)
               (let ((process (make-pipe-process :name "rd-pipe")))
                 (prog1 (attach process) (delete-process process)))
               ,(remoteterm-tests--socket-pair-form '(attach server)))
       (x-close-connection "remote-attach"))))

(rd-daemon-deftest remoteterm-tests-attach-sequence
  "Each draw goes to the connection that a model of the attaches gives.
The operations attach, detach, draw, and close two connections.  The
redisplay after an attach draws all of the frame for the new client.  A
refused attach keeps the client.  A process can attach again after a
detach.  A closed connection gets no bytes, and the session gives no
error.  When the connection of the client closes, the session has no
client, and keeps its frame."
  (let ((rd-gen-trials 12))
    (rd-gen-for-all #'remoteterm-tests--attach-operations
                    #'remoteterm-tests--check-attaches)))

(rd-daemon-deftest remoteterm-tests-attach-sentinel
  "Only a close of its connection detaches the client of a session.
These are the examples of `remoteterm-tests-attach-sequence' for the
sentinel of a client: an event that does not close the client, a close
of the client, a close of another process, and a close of a process
after its detach."
  (let* ((examples (list '((attach a) (event a) (draw))
                         '((attach a) (close a) (draw))
                         '((attach a) (close b) (draw))
                         '((attach a) (close-peer a) (attach b))
                         '((attach a) (detach) (close a) (attach b))))
         (rd-gen-trials (length examples)))
    (rd-gen-for-all (lambda () (pop examples))
                    #'remoteterm-tests--check-attaches)))

(defun remoteterm-tests--check-attaches (operations)
  "Check that OPERATIONS on a session give what the model gives."
  (pcase-let* (((map (:draws expected) (:errors errors)
                     (:clients expected-clients))
                (remoteterm-tests--attach-model operations))
               ((map :received :refused :first :later :clients :live)
                (rd-daemon-eval
                 (remoteterm-tests--attach-sequence-form operations))))
    (should (> (length later) 0))
    (should (equal clients expected-clients))
    (should live)
    (should (equal refused
                   (mapcar (lambda (error)
                             (cons (car error)
                                   (alist-get
                                    (cdr error)
                                    remoteterm-tests--attach-errors)))
                           errors)))
    (dolist (name '(a b))
      (should (equal (alist-get name received)
                     (mapconcat (lambda (draw)
                                  (if (eq draw 'first) first later))
                                (alist-get name expected)))))))

(defun remoteterm-tests--attach-operations ()
  "Return a random list of operations on the connections A and B.
An operation is (attach NAME), (detach), (draw), (close NAME),
\(close-peer NAME), or (event NAME).  attach attaches NAME, and then
runs redisplay if the attach succeeds.  close closes the connection
on the side of the session.  close-peer closes the other end, and
then draws.  event calls the sentinel of NAME with an event that does
not close NAME."
  (cl-loop repeat (rd-gen-integer 1 12)
           collect (pcase (rd-gen-random 7)
                     ((or 0 1) (list 'attach (rd-gen-element '(a b))))
                     (2 '(detach))
                     (3 '(draw))
                     (4 (list 'event (rd-gen-element '(a b))))
                     (_ (list (rd-gen-element '(close close-peer))
                              (rd-gen-element '(a b)))))))

(defun remoteterm-tests--attach-model (operations)
  "Return the model of the draws, errors, and clients of OPERATIONS.
The value is a plist.  :draws is an alist from each
connection name to the list of its draws.  A draw is `first' if it is
the redisplay after an attach, which draws all of the frame, and
`later' if not.  :errors is the list of (INDEX . KIND) of each refused
attach, where INDEX is the position of the operation and KIND is a key
of `remoteterm-tests--attach-errors'.  :clients is the list of the
name of the client after each operation, or nil if the session has no
client."
  (let ((client nil) (closed nil) (draws nil) (errors nil) (clients nil)
        (index 0))
    (cl-flet ((draw (kind)
                (when (and client (not (memq client closed)))
                  (push kind (alist-get client draws)))))
      (dolist (operation operations)
        (pcase operation
          (`(attach ,name)
           (cond ((memq name closed) (push (cons index 'closed) errors))
                 ((eq name client) (push (cons index 'attached) errors))
                 (t (setq client name)
                    (draw 'first))))
          ('(detach) (setq client nil))
          ('(draw) (draw 'later))
          (`(close ,name) (push name closed))
          (`(close-peer ,name) (push name closed) (draw 'later))
          (`(event ,_)))
        (when (memq client closed)
          (setq client nil))
        (push client clients)
        (cl-incf index)))
    (list :draws (mapcar (lambda (name)
                           (cons name (reverse (alist-get name draws))))
                         '(a b))
          :errors (nreverse errors)
          :clients (nreverse clients))))

(defun remoteterm-tests--attach-sequence-form (operations)
  "Return a form for the daemon that does OPERATIONS on a session.
The value of the form is a plist.  :received is an alist from each
connection name to the bytes that it got.  :refused is the list of
\(INDEX . MESSAGE) of each refused attach.  :clients is the list of
the name of the client after each operation, nil if the session has no
client, or the process of a client that is not A or B.  :live is t if
the frame is live at the end.  :first and :later are the
bytes of the first draw after an attach and of a later draw, from a
recording client."
  (remoteterm-tests--frame-form
   '((width . 40) (height . 10))
   '(insert "abc\ndef\n")
   `(remoteterm-tests--run-attaches ',operations display frame)))

(defun remoteterm-tests--run-attaches (operations display frame)
  "Do OPERATIONS on the session DISPLAY, which shows FRAME.
The value is as for `remoteterm-tests--attach-sequence-form'.  Each
draw is small, so the connections take all the bytes with no read
until the end."
  (cl-flet ((draw ()
              (redraw-frame frame)
              (redisplay t)
              (remote-display--take-output display)))
    (remote-display--record display)
    (let ((first (draw))
          (later (draw))
          (refused nil)
          (clients nil)
          (index 0))
      (remote-display--detach display)
      (remoteterm-tests--with-connections
       '(a b)
       (lambda (connections)
         (cl-flet ((side (name key) (plist-get (alist-get name connections) key))
                   (redraw () (redraw-frame frame) (redisplay t)))
           (dolist (operation operations)
             (pcase operation
               (`(attach ,name)
                (condition-case err
                    (progn
                      (remote-display-attach display (side name :server-side))
                      (redisplay t))
                  (error (push (cons index (cadr err)) refused))))
               ('(detach) (remote-display--detach display))
               ('(draw) (redraw))
               (`(close ,name) (delete-process (side name :server-side)))
               (`(event ,name)
                (let ((process (side name :server-side)))
                  (when (process-live-p process)
                    (funcall (process-sentinel process) process "open\n"))))
               (`(close-peer ,name)
                (remoteterm-tests--drain (side name :client-side)
                                         (side name :chunks))
                (delete-process (side name :client-side))
                (redraw)
                (remoteterm-tests--wait-closed (side name :server-side))))
             (let ((client (remote-display-client display)))
               (push (or (car (rassq client
                                     (mapcar (lambda (name)
                                               (cons name
                                                     (side name :server-side)))
                                             '(a b))))
                         client)
                     clients))
             (cl-incf index))
           (remote-display--detach display)
           (list :received
                 (mapcar (lambda (name)
                           (cons name (remoteterm-tests--drain
                                       (side name :client-side)
                                       (side name :chunks))))
                         '(a b))
                 :refused (nreverse refused)
                 :clients (nreverse clients)
                 :live (frame-live-p frame)
                 :first first
                 :later later)))))))

(defun remoteterm-tests--with-connections (names function)
  "Call FUNCTION with a connection on a local socket for each of NAMES.
The argument of FUNCTION is an alist from each name to a plist.
:server-side is the connection that the server accepted, :client-side
is the other end, and :chunks is a function that gives the strings
that the other end got, the last first."
  (let* ((socket (make-temp-name
                  (expand-file-name "rd-socket" temporary-file-directory)))
         (accepted nil)
         (server (make-network-process
                  :name "rd-server" :server t :family 'local
                  :service socket :coding 'binary
                  :log (lambda (_server connection _message)
                         (push connection accepted))))
         (connections nil))
    (unwind-protect
        (progn
          (dolist (name names)
            (let* ((chunks (list nil))
                   (client-side
                    (make-network-process
                     :name "rd-client" :family 'local
                     :service socket :coding 'binary
                     :filter (lambda (_process string)
                               (push string (car chunks))))))
              (with-timeout (10 (error "The server accepted no connection"))
                (while (< (length accepted) (1+ (length connections)))
                  (accept-process-output nil 0.05)))
              (push (list name
                          :server-side (car accepted)
                          :client-side client-side
                          :chunks (lambda () (car chunks)))
                    connections)))
          (funcall function (nreverse connections)))
      (dolist (connection connections)
        (delete-process (plist-get (cdr connection) :server-side))
        (delete-process (plist-get (cdr connection) :client-side)))
      (delete-process server)
      (delete-file socket))))

(defun remoteterm-tests--wait-closed (process)
  "Read until Emacs sees that the other end of PROCESS is closed."
  (with-timeout (10 (error "The connection did not close"))
    (while (process-live-p process)
      (accept-process-output nil 0.05))))

(defun remoteterm-tests--drain (process chunks)
  "Read from PROCESS until it gets no more bytes, and return all its bytes.
CHUNKS is a function that gives the strings that PROCESS got, the last
first."
  (when (process-live-p process)
    (while (accept-process-output process 0.1)))
  (string-to-unibyte (apply #'concat (reverse (funcall chunks)))))

(defun remoteterm-tests--process-sink-case ()
  "Return a random list of (LINES REDRAWS).
LINES is a list of lines of text, and REDRAWS is the number of
redraws of the frame before the other end reads.  Half of the cases
have many redraws, so the bytes fill the buffer of the socket, and the
session must keep the bytes that the connection cannot take."
  (list (cl-loop repeat (rd-gen-integer 1 30)
                 collect (remoteterm-tests--text))
        (if (zerop (rd-gen-random 2))
            (rd-gen-integer 1 3)
          (rd-gen-integer 30 60))))

(defun remoteterm-tests--process-sink-form (lines redraws)
  "Return a form for the daemon that draws LINES to two clients.
The frame draws REDRAWS times to a recording client, and then REDRAWS
times to a network process.  The other end of the connection reads
only after the last draw.  The value of the form is a plist.
:recorded is the bytes of the recording client, and :received is
the bytes that the other end got."
  (remoteterm-tests--frame-form
   '((width . 80) (height . 40))
   `(dolist (line ',lines) (insert line "\n"))
   (remoteterm-tests--socket-pair-form
    `(cl-flet ((draw ()
                 (dotimes (_ ,redraws)
                   (redraw-frame frame)
                   (redisplay t))))
       (unwind-protect
           (progn
             (remote-display--record display)
             (draw)
             (let ((recorded (remote-display--take-output display)))
               (remote-display-attach display server-side)
               (draw)
               (remoteterm-tests--read-until client-side
                                             (lambda () received)
                                             (length recorded))
               (list :recorded recorded
                     :received (string-to-unibyte
                                (apply #'concat (reverse received))))))
         (remote-display--detach display))))))

(defun remoteterm-tests--socket-pair-form (body)
  "Return a form that evaluates BODY with a connection on a local socket.
BODY can use the variables SERVER-SIDE, the connection that the server
accepted, and CLIENT-SIDE, the other end.  The list RECEIVED has the
strings that CLIENT-SIDE got, the last first.  The value of the form
is the value of BODY."
  `(let* ((socket (make-temp-name
                   (expand-file-name "rd-socket" temporary-file-directory)))
          (server-side nil)
          (received nil)
          (server (make-network-process
                   :name "rd-server" :server t :family 'local
                   :service socket :coding 'binary
                   :log (lambda (_server connection _message)
                          (setq server-side connection))))
          (client-side (make-network-process
                        :name "rd-client" :family 'local
                        :service socket :coding 'binary
                        :filter (lambda (_process string)
                                  (push string received)))))
     (unwind-protect
         (progn
           (with-timeout (10 (error "The server accepted no connection"))
             (while (not server-side)
               (accept-process-output nil 0.05)))
           ,body)
       (when server-side
         (delete-process server-side))
       (delete-process client-side)
       (delete-process server)
       (delete-file socket))))

(defun remoteterm-tests--read-until (process received size)
  "Read from PROCESS until the strings that RECEIVED gives have SIZE bytes.
RECEIVED is a function with no arguments.  Then read once more, so
that extra bytes also arrive.  Stop after 10 seconds."
  (with-timeout (10)
    (while (< (apply #'+ (mapcar #'length (funcall received))) size)
      (accept-process-output process 0.05)))
  (accept-process-output process 0.05))

(rd-daemon-deftest remoteterm-tests-attach-redraws
  "The first redisplay after an attach draws each frame fully.
A client draws the frame, and then detaches.  While no client is
attached, random edits change the buffer, point moves, the frame
changes its size, and redisplay can run.  Law: the bytes of the first redisplay after an
attach are equal to the bytes of `redraw-frame' and a redisplay for a
new client."
  (let ((rd-gen-trials 12))
    (rd-gen-for-all
     #'remoteterm-tests--detached-operations
     (lambda (operations)
       (pcase-let ((`(,attached ,redrawn)
                    (rd-daemon-eval
                     (remoteterm-tests--frame-form
                      '((width . 30) (height . 6))
                      '(insert "abc\ndef\n")
                      `(remoteterm-tests--attach-after
                        ',operations display frame buffer)))))
         (should (> (length redrawn) 0))
         (should (equal attached redrawn)))))))

(defun remoteterm-tests--detached-operations ()
  "Return a random list of operations on a frame with no client.
An operation is (insert POSITION STRING), (delete COUNT), (goto
POSITION), (resize COLUMNS LINES), (message STRING), (redraw),
\(sit), or (redisplay).  (sit) is `sit-for' with no time, and (redraw)
is `redraw-frame'."
  (cl-loop repeat (rd-gen-integer 0 8)
           collect (pcase (rd-gen-random 9)
                     (0 (list 'insert (rd-gen-integer 1 60)
                              (rd-gen-element
                               '("x" "yz\n" "\n" "\u00e9\u4e2d"
                                 "a line that is longer than the window\n"))))
                     (1 (list 'delete (rd-gen-integer 1 8)))
                     (2 (list 'goto (rd-gen-integer 1 60)))
                     (3 (list 'resize (rd-gen-integer 20 40)
                              (rd-gen-integer 4 8)))
                     (4 (list 'message (rd-gen-element '("m" "a\nb"))))
                     (5 '(redraw))
                     (6 '(sit))
                     (_ '(redisplay)))))

(defun remoteterm-tests--attach-after (operations display frame buffer)
  "Detach DISPLAY, do OPERATIONS, and attach a client again.
FRAME shows BUFFER, and DISPLAY has a client that drew FRAME.  The
value is (ATTACHED REDRAWN).  ATTACHED is the bytes of the first
redisplay after the attach.  REDRAWN is the bytes of `redraw-frame'
and a redisplay for one more new client."
  (remote-display--detach display)
  (remoteterm-tests--do-detached-operations operations frame buffer)
  (remote-display--record display)
  (redisplay t)
  (let ((attached (remote-display--take-output display)))
    (remote-display--record display)
    (redraw-frame frame)
    (redisplay t)
    (list attached (remote-display--take-output display))))

(defun remoteterm-tests--do-detached-operations (operations frame buffer)
  "Do OPERATIONS of `remoteterm-tests--detached-operations' on BUFFER.
FRAME shows BUFFER."
  (let ((window (frame-root-window frame)))
    (with-current-buffer buffer
      (dolist (operation operations)
        (pcase operation
          (`(insert ,position ,string)
           (goto-char (min position (point-max)))
           (insert string))
          (`(delete ,count)
           (delete-region (point-min)
                          (min (point-max) (+ (point-min) count))))
          (`(goto ,position)
           (set-window-point window (min position (point-max))))
          (`(resize ,columns ,lines)
           (set-frame-size frame columns lines))
          (`(message ,string) (message "%s" string))
          ('(redraw) (redraw-frame frame))
          ('(sit) (sit-for 0))
          ('(redisplay) (redisplay t)))))))

;; The number of mode lines of the current buffer that redisplay drew
;; with `remoteterm-tests--count-mode-line'.
(defvar-local remoteterm-tests--mode-lines 0)

(defun remoteterm-tests--count-mode-line ()
  "Count a mode line that redisplay draws, and return an empty string."
  (cl-incf remoteterm-tests--mode-lines)
  "")

(rd-daemon-deftest remoteterm-tests-detached-no-redisplay
  "Redisplay does not draw a remote frame while no client is attached.
A client draws the frame, and then detaches.  While no client is
attached, random edits, moves of point, resizes, and redisplays occur.
A mode line counts its draws.  Law: the count does not change while no
client is attached, and it grows at the first redisplay after an
attach.  The test does this twice."
  (let ((rd-gen-trials 12))
    (rd-gen-for-all
     #'remoteterm-tests--detached-operations
     (lambda (operations)
       (pcase-let ((`(,before . ,cycles)
                    (rd-daemon-eval
                     (remoteterm-tests--frame-form
                      '((width . 30) (height . 6))
                      '(progn
                         (setq-local mode-line-format
                                     '(:eval
                                       (remoteterm-tests--count-mode-line)))
                         (insert "abc\ndef\n"))
                      `(remoteterm-tests--count-detached
                        ',operations display frame buffer)))))
         (should (> before 0))
         (pcase-dolist (`(,detached ,attached) cycles)
           (should (= detached 0))
           (should (> attached 0))))))))

(defun remoteterm-tests--count-detached (operations display frame buffer)
  "Count the mode lines that redisplay draws for OPERATIONS with no client.
DISPLAY, FRAME, and BUFFER are as for `remoteterm-tests--attach-after'.
Do a detach, OPERATIONS, and an attach two times.  The value is
\(BEFORE CYCLE CYCLE).  BEFORE is the number of mode lines that
redisplay drew before the first detach.  Each CYCLE is (DETACHED
ATTACHED).  DETACHED is the number of mode lines that redisplay drew
while no client was attached.  ATTACHED is the number for the first
redisplay after the attach."
  (cl-flet ((count ()
              (buffer-local-value 'remoteterm-tests--mode-lines buffer)))
    (cons (count)
          (cl-loop
           repeat 2
           collect (let ((before-detach (count)))
                     (remote-display--detach display)
                     (remoteterm-tests--do-detached-operations
                      operations frame buffer)
                     (redisplay t)
                     (let ((before-attach (count)))
                       (remote-display--record display)
                       (redisplay t)
                       (list (- before-attach before-detach)
                             (- (count) before-attach))))))))

(defconst remoteterm-tests--display-queries
  '(display-graphic-p display-color-p display-grayscale-p
    display-pixel-width display-pixel-height display-mm-width
    display-mm-height display-planes display-color-cells
    display-visual-class display-screens display-backing-store
    display-save-under display-monitor-attributes-list
    frame-monitor-attributes display-images-p display-mouse-p
    display-popup-menus-p display-selections-p font-family-list)
  "The display queries that take a frame and that Lisp code can call.")

(rd-daemon-deftest remoteterm-tests-detach-keeps-display-info
  "A detach and an attach do not change the answers to display queries.
A frame of a random size asks each of `remoteterm-tests--display-queries'
with a client, with no client, and with a new client.  Law: the three
answers are equal."
  (let ((rd-gen-trials 12))
    (rd-gen-for-all
     (lambda () (list (rd-gen-integer 20 80) (rd-gen-integer 4 30)))
     (lambda (size)
       (should-not
        (rd-daemon-eval
         (remoteterm-tests--frame-form
          `((width . ,(car size)) (height . ,(cadr size)))
          '(insert "abc\n")
          `(remoteterm-tests--display-info-changes
            ',remoteterm-tests--display-queries display frame))))))))

(defun remoteterm-tests--display-info-changes (queries display frame)
  "Return the QUERIES that a detach or an attach of DISPLAY changes.
Call each function of QUERIES with FRAME before a detach, after the
detach, and after an attach.  The value is a list of (QUERY ATTACHED
DETACHED REATTACHED) for each QUERY whose answers are not all equal.
The list has each answer as a string from `format' with \"%S\", so
that the value can go to the test."
  (cl-flet ((answers ()
              (mapcar (lambda (query) (funcall query frame)) queries)))
    (let* ((attached (answers))
           (detached (progn (remote-display--detach display) (answers)))
           (reattached (progn (remote-display--record display)
                              (answers))))
      (cl-loop for query in queries
               for a in attached
               for d in detached
               for r in reattached
               unless (and (equal a d) (equal d r))
               collect (list query (format "%S" a) (format "%S" d)
                             (format "%S" r))))))

(defun remoteterm-tests--frame-parameters ()
  "Return random parameters for a remote frame.
The parameters set the size, the menu bar, and the tab bar."
  `((width . ,(rd-gen-integer 20 80))
    (height . ,(rd-gen-integer 6 30))
    (menu-bar-lines . ,(rd-gen-random 2))
    (tab-bar-lines . ,(rd-gen-random 2))))

(rd-daemon-deftest remoteterm-tests-frame-geometry
  "The edges of a remote frame agree with its windows and its size.
Laws: the outer edges contain the native edges, and the native edges
contain the inner edges.  The edges of no type are the native edges.
The native size is the pixel size of the frame.  The inner edges are
the edges of the area of the windows, from the top of the root window
to the bottom of the minibuffer window.  The bars of `frame-geometry'
above the windows fill the space between the native top and the inner
top.  The outer size of `frame-geometry' is the size of the outer
edges.  The height of the menu bar is the value of `menu-bar-lines'
times the height of a line of the frame."
  (let ((rd-gen-trials 12))
    (rd-gen-for-all
     #'remoteterm-tests--frame-parameters
     (lambda (parameters)
       (pcase-let*
           (((map :outer :native :inner :default :size :windows :bars
                  :menu-bar :char-height :outer-size)
             (rd-daemon-eval
              (remoteterm-tests--frame-form
               parameters '(insert "abc\n")
               '(remoteterm-tests--geometry frame))))
            (`(,left ,top . ,_) native))
         (should (remoteterm-tests--edges-within-p native outer))
         (should (equal default native))
         (should (remoteterm-tests--edges-within-p inner native))
         (should (equal size (list (- (nth 2 native) left)
                                   (- (nth 3 native) top))))
         (should (equal inner (cl-mapcar #'+ (list left top left top)
                                         windows)))
         (should (= bars (- (nth 1 inner) top)))
         (should (= menu-bar (* (alist-get 'menu-bar-lines parameters)
                                char-height)))
         (should (equal outer-size (list (- (nth 2 outer) (nth 0 outer))
                                         (- (nth 3 outer)
                                            (nth 1 outer))))))))))

(defun remoteterm-tests--geometry (frame)
  "Return the geometry of FRAME as a plist.
:outer, :native, and :inner are the edges of each type from
`frame-edges', and :default is the edges for no type.  :size is the
pixel width and height of FRAME.
:windows is the edges of the area of the windows of FRAME, relative to
the native edges.  :bars is the sum of the heights of the bars of
`frame-geometry' above the windows.  :menu-bar is the height of the
menu bar, and :char-height is the height of a line of FRAME.
:outer-size is the outer size from `frame-geometry'."
  (let* ((root (window-pixel-edges (frame-root-window frame)))
         (minibuffer (window-pixel-edges (minibuffer-window frame)))
         (geometry (frame-geometry frame))
         (tool-bar-top (eq (alist-get 'tool-bar-position geometry) 'top)))
    (list :outer (frame-edges frame 'outer-edges)
          :native (frame-edges frame 'native-edges)
          :inner (frame-edges frame 'inner-edges)
          :default (frame-edges frame)
          :size (list (frame-pixel-width frame) (frame-pixel-height frame))
          :windows (list (nth 0 root) (nth 1 root)
                         (nth 2 root) (nth 3 minibuffer))
          :bars (+ (cdr (alist-get 'menu-bar-size geometry))
                   (cdr (alist-get 'tab-bar-size geometry))
                   (if tool-bar-top
                       (cdr (alist-get 'tool-bar-size geometry))
                     0))
          :menu-bar (cdr (alist-get 'menu-bar-size geometry))
          :char-height (frame-char-height frame)
          :outer-size (let ((size (alist-get 'outer-size geometry)))
                        (list (car size) (cdr size))))))

(rd-daemon-deftest remoteterm-tests-menu-bar-mode
  "The menu bar of a remote frame follows the last change to it.
A trial applies a random list of changes to a remote frame.  A change
is a call of `menu-bar-mode' or a change of the `menu-bar-lines'
parameter of the frame.  Some parameter values are not a number of
lines: nil, a negative number, or a number that is not an integer.
Law: after each change, the height of the menu bar from
`frame-geometry' is the number of lines that the last change gives,
times the height of a line of the frame.  A value that is not a
number of lines gives no menu bar."
  (let ((rd-gen-trials 12))
    (rd-gen-for-all
     (lambda ()
       (rd-gen-list #'remoteterm-tests--menu-bar-change 6))
     (lambda (changes)
       (pcase-let ((`(,char-height . ,heights)
                    (rd-daemon-eval
                     `(remoteterm-tests--menu-bar-heights ',changes))))
         (should (equal heights
                        (mapcar (lambda (change)
                                  (* (remoteterm-tests--menu-bar-lines
                                      change)
                                     char-height))
                                changes))))))))

(defun remoteterm-tests--menu-bar-change ()
  "Return a random change to the menu bar of a frame.
The change is (mode ARG), a call of `menu-bar-mode' with ARG, or
\(lines N), a change of the `menu-bar-lines' parameter to N.  N is a
number of lines from 0 to 2, or a value that is not a number of
lines."
  (if (zerop (rd-gen-random 2))
      (list 'mode (rd-gen-element '(1 -1)))
    (list 'lines (rd-gen-element (list (rd-gen-random 3) nil -1 1.5)))))

(defun remoteterm-tests--menu-bar-lines (change)
  "Return the number of menu bar lines that CHANGE gives.
This is the model of the menu bar.  A value that is not a natural
number gives 0 lines."
  (pcase change
    (`(mode ,arg) (if (> arg 0) 1 0))
    (`(lines ,n) (if (natnump n) n 0))))

(defun remoteterm-tests--menu-bar-heights (changes)
  "Apply CHANGES to a new remote frame, and return the menu bar heights.
The value is (CHAR-HEIGHT . HEIGHTS).  CHAR-HEIGHT is the height of a
line of the frame.  HEIGHTS has the height of the menu bar from
`frame-geometry' after each change.  The function restores
`menu-bar-mode' and `default-frame-alist' at the end."
  (let ((mode menu-bar-mode)
        (defaults default-frame-alist))
    (remoteterm-tests--with-display "remote-menu-bar"
      (remoteterm-tests--with-frame frame "remote-menu-bar" nil
        (unwind-protect
            (cons (frame-char-height frame)
                  (mapcar (lambda (change)
                            (remoteterm-tests--apply-menu-bar-change
                             frame change)
                            (cdr (alist-get 'menu-bar-size
                                            (frame-geometry frame))))
                          changes))
          (menu-bar-mode (if mode 1 -1))
          (setq default-frame-alist defaults))))))

(defun remoteterm-tests--apply-menu-bar-change (frame change)
  "Apply CHANGE to the menu bar of FRAME."
  (pcase change
    (`(mode ,arg) (menu-bar-mode arg))
    (`(lines ,n) (set-frame-parameter frame 'menu-bar-lines n))))

(rd-daemon-deftest remoteterm-tests-menu-bar-draws
  "Law M1: redisplay draws the menu bar only in its rows.
A trial makes a remote frame with a random number of menu bar lines,
0 or 1, and then changes that number a random number of times.  After
the first redisplay and after each change, the output of the
redisplay obeys these laws:
 - Each draw-glyphs message has a clip inside the rows of the menu
   bar, or below the top of the windows.
 - With a menu bar, at least one draw-glyphs message is inside the
   rows of the menu bar.
 - The clear-area messages cover the rows of the old and of the new
   menu bar on the full width of the frame.  Thus no stale menu bar
   text stays after the menu bar goes away."
  (let ((rd-gen-trials 8))
    (rd-gen-for-all
     #'remoteterm-tests--menu-bar-toggles
     (lambda (toggles)
       (let ((old-height 0))
         (dolist (step (remoteterm-tests--menu-bar-draw-steps toggles))
           (remoteterm-tests--check-menu-bar-draw step old-height)
           (setq old-height (plist-get step :height))))))))

(defun remoteterm-tests--menu-bar-toggles ()
  "Return a random list of menu bar line counts, each 0 or 1.
Each count is not the same as the count before it, so each element
after the first is a change."
  (let ((lines (rd-gen-random 2)))
    (cons lines
          (cl-loop repeat (rd-gen-integer 1 3)
                   collect (setq lines (- 1 lines))))))

(defun remoteterm-tests--menu-bar-draw-steps (toggles)
  "Draw a remote frame with each menu bar line count of TOGGLES.
The first count is a parameter of the new frame, and each other count
is a change of the `menu-bar-lines' parameter.  Return a plist for
each count.  :lines is the count, :height is the height of the menu
bar, :char-height is the height of a line, :top is the top of the
windows, :width is the width of the frame, and :output is the bytes
of the redisplay after the change."
  (remoteterm-tests--with-display "remote-menu-draw"
    (remoteterm-tests--with-frame frame "remote-menu-draw"
                                  `((menu-bar-lines . ,(car toggles))
                                    (width . 60) (height . 10))
      (let ((buffer (generate-new-buffer "menu-draw")))
        (unwind-protect
            (progn
              (with-current-buffer buffer (insert "abc\ndef\n"))
              (set-window-buffer (frame-root-window frame) buffer)
              (select-frame frame)
              (remote-display--focus frame)
              (remote-display--record "remote-menu-draw")
              (cons (remoteterm-tests--menu-bar-draw-step
                     frame (car toggles))
                    (mapcar (lambda (lines)
                              (set-frame-parameter frame 'menu-bar-lines
                                                   lines)
                              (remoteterm-tests--menu-bar-draw-step
                               frame lines))
                            (cdr toggles))))
          (kill-buffer buffer))))))

(defun remoteterm-tests--menu-bar-draw-step (frame lines)
  "Redisplay FRAME, and return the plist of a step with LINES lines.
See `remoteterm-tests--menu-bar-draw-steps'."
  (redisplay t)
  (list :lines lines
        :height (cdr (alist-get 'menu-bar-size (frame-geometry frame)))
        :char-height (frame-char-height frame)
        :top (nth 1 (window-pixel-edges (frame-root-window frame)))
        :width (frame-native-width frame)
        :output (remote-display--take-output "remote-menu-draw")))

(defun remoteterm-tests--check-menu-bar-draw (step old-height)
  "Check the laws of `remoteterm-tests-menu-bar-draws' for STEP.
OLD-HEIGHT is the height of the menu bar before the change of STEP."
  (pcase-let* (((map :lines :height :char-height :top :width :output) step)
               (menu-bar (list 0 0 width height))
               (clips (mapcar #'remoteterm-tests--clip
                              (remoteterm-tests--of-type 'draw-glyphs
                                                         output))))
    (should (= height (* lines char-height)))
    (dolist (clip clips)
      (should (or (remoteterm-tests--inside-p clip menu-bar)
                  (<= top (nth 1 clip)))))
    (when (= lines 1)
      (should (cl-some (lambda (clip)
                         (remoteterm-tests--inside-p clip menu-bar))
                       clips)))
    (should (remoteterm-tests--covered-p
             (list 0 0 width (max height old-height))
             (mapcar #'remoteterm-tests--clear-rectangle
                     (remoteterm-tests--of-type 'clear-area output))))))

(defun remoteterm-tests--clear-rectangle (fields)
  "Return the rectangle of the clear-area FIELDS as (LEFT TOP RIGHT BOTTOM)."
  (pcase-let (((map :x :y :width :height) fields))
    (list x y (+ x width) (+ y height))))

(defun remoteterm-tests--covered-p (area rectangles)
  "Return non-nil if the union of RECTANGLES covers the rectangle AREA.
Each is a list (LEFT TOP RIGHT BOTTOM).  Check each pixel row of AREA."
  (pcase-let ((`(,left ,top ,right ,bottom) area))
    (cl-loop for y from top below bottom
             always (remoteterm-tests--row-covered-p
                     left right
                     (seq-filter (lambda (rectangle)
                                   (and (<= (nth 1 rectangle) y)
                                        (< y (nth 3 rectangle))))
                                 rectangles)))))

(defun remoteterm-tests--row-covered-p (left right rectangles)
  "Return non-nil if RECTANGLES cover the pixels from LEFT to RIGHT.
Each rectangle is a list (LEFT TOP RIGHT BOTTOM) that contains the row."
  (let ((x left))
    (dolist (rectangle (sort (copy-sequence rectangles)
                             (lambda (a b) (< (car a) (car b)))))
      (when (<= (nth 0 rectangle) x)
        (setq x (max x (nth 2 rectangle)))))
    (>= x right)))

(rd-daemon-deftest remoteterm-tests-menu-bar-click
  "Law M2: a press on a menu bar item gives the event of that item.
A trial picks a random item K of the menu bar of a remote frame.  A
button message with button 1 and press 1 at the left pixel of K in
the first row gives the prefix event `menu-bar' and then an event
with the head K, whose position is in the area `menu-bar'.  The
release after it gives no event.  The model of the positions of the
items is `menu-bar-item-at-x'."
  (let ((rd-gen-trials 8))
    (rd-gen-for-all
     (lambda () (rd-gen-random 1000))
     (lambda (choice)
       (pcase-let ((`(,key ,events) (remoteterm-tests--menu-bar-click
                                     choice)))
         (should (= (length events) 2))
         (should (eq (car events) 'menu-bar))
         (should (eq (car-safe (cadr events)) key))
         (should (eq (posn-area (event-start (cadr events)))
                     'menu-bar)))))))

(defun remoteterm-tests--menu-bar-click (choice)
  "Click on an item of the menu bar of a remote frame.
CHOICE modulo the number of items selects the item.  Return a list
\(KEY EVENTS): KEY is the key of the item, and EVENTS is the list of
the events of the press and of the release."
  (remoteterm-tests--with-input-session
   (lambda (frame client-side &optional _server-side)
     (set-frame-parameter frame 'width 120)
     (set-frame-parameter frame 'menu-bar-lines 1)
     (redisplay t)
     (pcase-let* ((items (with-current-buffer
                             (window-buffer (frame-selected-window frame))
                           (remoteterm-tests--menu-bar-items
                            (frame-width frame))))
                  (`(,key . ,column) (nth (mod choice (length items))
                                          items))
                  (x (* column (frame-char-width frame))))
       (process-send-string
        client-side
        (mapconcat
         (lambda (step)
           (remote-display--encode
            `(button :frame ,(remoteterm-tests--frame-id frame)
                     :time ,(car step) :x ,x :y 0 :button 1 :modifiers 0
                     :press ,(cdr step))))
         '((1000 . 1) (1050 . 0))))
       (list key (remoteterm-tests--read-events 3))))))

(defun remoteterm-tests--menu-bar-items (columns)
  "Return the items of the menu bar in the first COLUMNS columns.
Each item is (KEY . COLUMN), with the left column of the item, as
`menu-bar-item-at-x' gives it."
  (seq-uniq (delq nil (mapcar #'menu-bar-item-at-x
                              (number-sequence 0 (1- columns))))))

(rd-daemon-deftest remoteterm-tests-frame-edges-not-remote
  "The edges and the geometry of a remote frame refuse other frames.
The other frame is the initial frame of the daemon."
  (let ((rd-gen-trials 4))
    (rd-gen-for-all
     (lambda () (rd-gen-element '(remote-frame-edges remote-frame-geometry)))
     (lambda (function)
       (should (eq (rd-daemon-eval
                    `(condition-case nil
                         (progn (,function terminal-frame) 'no-error)
                       (error 'error)))
                   'error))))))

(defun remoteterm-tests--edges-within-p (inner outer)
  "Return non-nil if the edges INNER are within the edges OUTER.
Each argument is a list (LEFT TOP RIGHT BOTTOM)."
  (pcase-let ((`(,l1 ,t1 ,r1 ,b1) inner)
              (`(,l2 ,t2 ,r2 ,b2) outer))
    (and (<= l2 l1) (<= t2 t1) (<= r1 r2) (<= b1 b2))))

(defconst remoteterm-tests--dots-per-inch 96
  "The resolution of a remote session, in dots per inch.")

(rd-daemon-deftest remoteterm-tests-display-answers
  "The display queries of frame.el agree with a remote display.
Laws: the number of colors is 2 to the power of the number of planes,
and the visual class is `true-color'.  The size in millimeters is the
size in pixels at `remoteterm-tests--dots-per-inch', with a tolerance
of 1 millimeter.
A remote display has a mouse and symbol keys.  It has popup menus
exactly when it has a mouse."
  (let ((rd-gen-trials 12))
    (rd-gen-for-all
     #'remoteterm-tests--frame-parameters
     (lambda (parameters)
       (pcase-let
           (((map :cells :planes :class :pixels :mm :mouse :popup :keys)
             (rd-daemon-eval
              (remoteterm-tests--frame-form
               parameters '(insert "abc\n")
               '(remoteterm-tests--display-answers frame)))))
         (should (equal cells (expt 2 planes)))
         (should (eq class 'true-color))
         (should (cl-every (lambda (pixels mm)
                             (and (numberp mm)
                                  (<= (abs (- mm (remoteterm-tests--mm pixels)))
                                      1)))
                           pixels mm))
         (should (eq mouse t))
         (should (eq popup mouse))
         (should keys))))))

(defun remoteterm-tests--mm (pixels)
  "Return the millimeters of PIXELS at `remoteterm-tests--dots-per-inch'."
  (/ (* pixels 25.4) remoteterm-tests--dots-per-inch))

(defun remoteterm-tests--display-answers (frame)
  "Return the answers of the display queries of frame.el for FRAME.
The value is a plist.  :pixels and :mm are each a list of the width
and the height of the display."
  (list :cells (display-color-cells frame)
        :planes (display-planes frame)
        :class (display-visual-class frame)
        :pixels (list (display-pixel-width frame)
                      (display-pixel-height frame))
        :mm (list (display-mm-width frame) (display-mm-height frame))
        :mouse (display-mouse-p frame)
        :popup (display-popup-menus-p frame)
        :keys (and (display-symbol-keys-p frame) t)))

;;;; Frame messages

(rd-daemon-deftest remoteterm-tests-frame-messages
  "A client knows each frame before its draws, and knows its size.
Each draw of a frame comes after a define-frame message for the frame,
after the last attach, and the last such message has the size of the
frame.  A define-frame message does not repeat the size that the
client knows.  A delete-frame message comes only for a frame that has
a definition, the session flushes it at once, and no draw of the frame
comes after it.  After each operation, the frames with a definition
are the frames of the session, and each definition has the size of
its frame."
  (let ((rd-gen-trials 30))
    (rd-gen-for-all
     #'remoteterm-tests--frame-operations
     (lambda (operations)
       (let ((steps (rd-daemon-eval
                     `(remoteterm-tests--run-frame-operations
                       ',operations))))
         (should (remoteterm-tests--draws (plist-get (car steps) :bytes)))
         (remoteterm-tests--check-frame-steps steps))))))

(defun remoteterm-tests--frame-operations ()
  "Return a random list of operations on the frames of a session.
An operation is (make), (delete I), (resize I WIDTH HEIGHT),
\(resize-same I), (attach), (attach-delete I) or (draw I).  I selects
a frame of the session, if it has a frame.  WIDTH and HEIGHT are in
pixels.  resize-same sets the size that the frame has.  attach-delete
attaches a client, and then deletes a frame before the client knows
it."
  (cl-loop repeat (rd-gen-integer 1 10)
           collect (pcase (rd-gen-random 7)
                     (0 '(make))
                     (1 (list 'delete (rd-gen-random 3)))
                     (2 (list 'resize (rd-gen-random 3)
                              (rd-gen-integer 100 900)
                              (rd-gen-integer 80 700)))
                     (3 '(attach))
                     (4 (list 'attach-delete (rd-gen-random 3)))
                     (5 (list 'resize-same (rd-gen-random 3)))
                     (_ (list 'draw (rd-gen-random 3))))))

(defun remoteterm-tests--run-frame-operations (operations)
  "Do OPERATIONS on the frames of a new session, and return the steps.
The session has one frame and a client at the start.  A session has
at most three frames.  A delete does nothing to the last frame,
because Emacs then deletes the terminal.  Each step is a plist.
:attach is non-nil if the operation attached a new client.  :bytes is
the bytes that the session sent for the operation and the redisplay
after it.  :frames is the list of (ID WIDTH HEIGHT) of each frame
after the step.  :unflushed is the list of the numbers of bytes that
the session did not flush after each delete of the step."
  (let ((display "remote-frames")
        (frames nil)
        (unflushed nil)
        (steps nil))
    (cl-labels
        ((pick (index)
           (and frames (nth (mod index (length frames)) frames)))
         (make ()
           (let ((frame (x-create-frame-with-faces
                         '((display . "remote-frames")))))
             (set-window-buffer (frame-root-window frame)
                                (generate-new-buffer "frames"))
             (select-frame frame)
             (setq frames (append frames (list frame)))))
         (forget (frame)
           (kill-buffer (window-buffer (frame-root-window frame)))
           (delete-frame frame))
         (delete (index)
           (when-let* ((frame (and (cdr frames) (pick index))))
             (setq frames (delq frame frames))
             (forget frame)
             (push (remote-display--unflushed-bytes display) unflushed)))
         (attach ()
           (remote-display--record display)
           (mapc #'redraw-frame frames))
         (operate (operation)
           (pcase operation
             ('(make) (when (< (length frames) 3) (make)))
             (`(delete ,index) (delete index))
             (`(attach-delete ,index) (attach) (delete index))
             (`(resize ,index ,width ,height)
              (when-let* ((frame (pick index)))
                (set-frame-size frame width height t)))
             (`(resize-same ,index)
              (when-let* ((frame (pick index)))
                (set-frame-size frame (frame-pixel-width frame)
                                (frame-pixel-height frame) t)))
             ('(attach) (attach))
             (`(draw ,index)
              (when-let* ((frame (pick index)))
                (with-current-buffer (window-buffer
                                      (frame-root-window frame))
                  (insert "abc\n"))))))
         (step (operation)
           (setq unflushed nil)
           (operate operation)
           (redisplay t)
           (list :attach (memq (car operation) '(attach attach-delete))
                 :bytes (remote-display--take-output display)
                 :frames (mapcar (lambda (frame)
                                   (list (remoteterm-tests--frame-id frame)
                                         (frame-pixel-width frame)
                                         (frame-pixel-height frame)))
                                 frames)
                 :unflushed unflushed)))
      (x-open-connection display)
      (unwind-protect
          (progn
            (make)
            (dolist (operation (cons '(attach) operations))
              (push (step operation) steps)))
        (mapc #'forget frames)
        (when (remoteterm-tests--terminal display)
          (x-close-connection display))))
    (nreverse steps)))

(defun remoteterm-tests--draws (bytes)
  "Return the messages in BYTES that draw on a frame."
  (seq-filter (lambda (message) (plist-get (cdr message) :frame))
              (remoteterm-tests--messages bytes)))

(defun remoteterm-tests--check-frame-steps (steps)
  "Check the laws of the frame messages in STEPS.
STEPS is as for `remoteterm-tests--run-frame-operations'."
  (let ((defined nil))
    (dolist (step steps)
      (when (plist-get step :attach)
        (setq defined nil))
      (should (cl-every #'zerop (plist-get step :unflushed)))
      (dolist (message (remoteterm-tests--all-messages
                        (plist-get step :bytes)))
        (let* ((id (plist-get (cdr message) :frame))
               (frame (assq id (plist-get step :frames))))
          (pcase (car message)
            ('define-frame
             (let ((size (list (plist-get (cdr message) :width)
                               (plist-get (cdr message) :height))))
               (should-not (equal (alist-get id defined) size))
               (setf (alist-get id defined) size)))
            ('delete-frame
             (should (assq id defined))
             (setq defined (assq-delete-all id defined)))
            (_ (when id
                 (should (assq id defined))
                 (when frame
                   (should (equal (assq id defined) frame))))))))
      (should (equal (sort (copy-sequence defined) :key #'car)
                     (sort (copy-sequence (plist-get step :frames))
                           :key #'car))))))

;;;; Frame memory and focus

(defconst remoteterm-tests--memory-cycles 300
  "The number of frames that a test of the frame memory measures.")

(defconst remoteterm-tests--memory-limit 10000
  "The most kilobytes that `remoteterm-tests--memory-cycles' frames can add.")

(defun remoteterm-tests--resident-memory ()
  "Return the resident memory of this process in kilobytes, or nil."
  (alist-get 'rss (process-attributes (emacs-pid))))

(defun remoteterm-tests--address-sanitizer-p ()
  "Return non-nil if this Emacs has AddressSanitizer.
AddressSanitizer keeps freed memory in a quarantine of 256 MB by
default, so the resident memory grows with no leak."
  (string-match-p "-fsanitize=address" system-configuration-options))

(defun remoteterm-tests--memory-growth (cycle)
  "Return the kilobytes that the process gains from calls of CYCLE.
First call CYCLE so that the heap reaches a steady size."
  (dotimes (_ (/ remoteterm-tests--memory-cycles 6))
    (funcall cycle))
  (garbage-collect)
  (let ((before (remoteterm-tests--resident-memory)))
    (dotimes (i remoteterm-tests--memory-cycles)
      (funcall cycle)
      (when (zerop (% i 100))
        (garbage-collect)))
    (garbage-collect)
    (- (remoteterm-tests--resident-memory) before)))

(ert-deftest remoteterm-tests-delete-frame-frees-faces ()
  "The memory of a deleted frame goes back to the process."
  (skip-unless (featurep 'remote-test))
  (skip-unless (remoteterm-tests--resident-memory))
  (skip-when (remoteterm-tests--address-sanitizer-p))
  (remoteterm-tests--with-display "remote-memory"
    (remoteterm-tests--with-frame kept "remote-memory" nil
      (should (< (remoteterm-tests--memory-growth
                  (lambda ()
                    (delete-frame
                     (x-create-frame-with-faces
                      '((display . "remote-memory"))))))
                 remoteterm-tests--memory-limit)))))

(ert-deftest remoteterm-tests-failed-frame-frees-faces ()
  "The memory of a frame that fails after its faces goes back to the process."
  (skip-unless (featurep 'remote-test))
  (skip-unless (remoteterm-tests--resident-memory))
  (skip-when (remoteterm-tests--address-sanitizer-p))
  (remoteterm-tests--with-display "remote-memory"
    (remoteterm-tests--with-frame kept "remote-memory" nil
      (should (< (remoteterm-tests--memory-growth
                  (lambda ()
                    (should-error
                     (x-create-frame
                      '((display . "remote-memory")
                        (width . (text-pixels . bad))))
                     :type 'wrong-type-argument)))
                 remoteterm-tests--memory-limit)))))

(defconst remoteterm-tests--malloc-cycles 50
  "The number of frames in one round of a measure of malloc.")

(defconst remoteterm-tests--malloc-rounds 8
  "The number of rounds of a measure of malloc.")

(defconst remoteterm-tests--frame-leak-limit 32
  "The most bytes that malloc can keep for each frame that went away.
Malloc uses at least this number of bytes for an object, so a leak of
one object for each frame reaches this limit.")

(defun remoteterm-tests--frame-leak (cycle)
  "Return the bytes that malloc keeps for each call of CYCLE.
Compare the states at the ends of the rounds, because a second garbage
collection frees more than the first one.  A leak shows in each round.
A new block of the heap shows in one round, so use the round with the
smallest gain."
  (let ((used (cl-loop repeat (1+ remoteterm-tests--malloc-rounds)
                       collect (remoteterm-tests--malloc-after-round cycle))))
    (/ (apply #'min (seq-mapn #'- (cdr used) used))
       remoteterm-tests--malloc-cycles)))

(defun remoteterm-tests--malloc-after-round (cycle)
  "Call CYCLE for one round, and return the bytes that malloc then holds."
  (dotimes (_ remoteterm-tests--malloc-cycles)
    (funcall cycle))
  (garbage-collect)
  (remote-display--malloc-used))

(ert-deftest remoteterm-tests-delete-frame-frees-all ()
  "A deleted frame leaves no bytes in malloc."
  (skip-unless (featurep 'remote-test))
  (skip-unless (remote-display--malloc-used))
  (remoteterm-tests--with-display "remote-memory"
    (remoteterm-tests--with-frame kept "remote-memory" nil
      (should (< (remoteterm-tests--frame-leak
                  (lambda ()
                    (delete-frame
                     (x-create-frame-with-faces
                      '((display . "remote-memory"))))))
                 remoteterm-tests--frame-leak-limit)))))

(ert-deftest remoteterm-tests-failed-frame-frees-all ()
  "A frame that fails while it is made leaves no bytes in malloc."
  (skip-unless (featurep 'remote-test))
  (skip-unless (remote-display--malloc-used))
  (remoteterm-tests--with-display "remote-memory"
    (remoteterm-tests--with-frame kept "remote-memory" nil
      (should (< (remoteterm-tests--frame-leak
                  (lambda ()
                    (ignore-error wrong-type-argument
                      (x-create-frame
                       '((display . "remote-memory")
                         (width . (text-pixels . bad)))))))
                 remoteterm-tests--frame-leak-limit)))))

(ert-deftest remoteterm-tests-delete-frame-clears-focus ()
  "A session does not keep a pointer to a deleted frame."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-focus"
    (remoteterm-tests--with-frame kept "remote-focus" nil
      (let ((focused (remoteterm-tests--make-frame "remote-focus" nil))
            (other (remoteterm-tests--make-frame "remote-focus" nil)))
        (remote-display--focus focused)
        (should (equal (remote-display--focus-frames "remote-focus")
                       (list focused focused)))
        (delete-frame other)
        (should (equal (remote-display--focus-frames "remote-focus")
                       (list focused focused)))
        (delete-frame focused)
        (should (equal (remote-display--focus-frames "remote-focus")
                       '(nil nil)))))))

;;;; Mouse highlight

(defun remoteterm-tests--mouse-highlight-form ()
  "Return a form for the daemon that deletes frames under a mouse highlight.
The form makes the frames SHOWN and SPARE on the session of a frame
that shows a text with a `mouse-face' property.  It moves the mouse to
the text in SHOWN, then deletes SPARE, then deletes SHOWN.  The value
of the form is a plist.  :entered, :after-spare, and :after-shown are
the state of the session at each step: `shown' if SHOWN is the frame of
the mouse highlight, nil if the session has no such frame, or `other'."
  (let ((parameters '((display . "remote-draw") (width . 40) (height . 10))))
    (remoteterm-tests--frame-form
     (cdr parameters)
     '(insert (propertize "mouse" 'mouse-face 'highlight))
     `(let* ((shown (x-create-frame-with-faces ',parameters))
             (spare (x-create-frame-with-faces ',parameters))
             (state (lambda ()
                      (let ((mouse (remote-display--mouse-frame display)))
                        (cond ((eq mouse shown) 'shown)
                              (mouse 'other)))))
             (shown-window (frame-root-window shown)))
        (set-window-buffer shown-window buffer)
        (redisplay t)
        (let ((edges (window-inside-pixel-edges shown-window)))
          (display--update-for-mouse-movement
           shown (1+ (nth 0 edges)) (1+ (nth 1 edges))))
        (list :entered (funcall state)
              :after-spare (progn (delete-frame spare) (funcall state))
              :after-shown (progn (delete-frame shown) (funcall state)))))))

(ert-deftest remoteterm-tests-delete-frame-clears-mouse-highlight ()
  "A session does not keep the deleted frame of its mouse highlight."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (rd-daemon-eval (remoteterm-tests--mouse-highlight-form))
                   '(:entered shown :after-spare shown :after-shown nil)))))

;;;; Sinks

(defun remoteterm-tests--sink-clear (frame)
  "Send one clear-area message for FRAME."
  (remote-display--clear-area frame 0 0 1 1))

(ert-deftest remoteterm-tests-sink-detach ()
  "A session without a sink sends nothing, and drops the recorded bytes."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-sink"
    (remoteterm-tests--with-frame frame "remote-sink" nil
      (remote-display--record "remote-sink")
      (remoteterm-tests--sink-clear frame)
      (should-not (equal (remote-display--take-output "remote-sink") ""))
      (remoteterm-tests--sink-clear frame)
      (remote-display--detach "remote-sink")
      (should-error (remote-display--take-output "remote-sink"))
      (remoteterm-tests--sink-clear frame)
      (should-error (remote-display--take-output "remote-sink"))
      ;; A new client does not get the bytes from the time with no client.
      (remote-display--record "remote-sink")
      (should (equal (remote-display--take-output "remote-sink") "")))))

(ert-deftest remoteterm-tests-sink-counted-bytes-no-client ()
  "A session with no client has no count of bytes."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-sink"
    (should-not (remote-display--counted-bytes "remote-sink"))))

(ert-deftest remoteterm-tests-sink-flush-update ()
  "After an update, a sink keeps no bytes."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let (((map :output :unflushed)
                 (rd-daemon-eval
                  (remoteterm-tests--frame-form
                   nil '(insert "abc")
                   '(list :output output
                          :unflushed (remote-display--unflushed-bytes
                                      display))))))
      (should (> (length output) 0))
      (should (= unflushed 0)))))

(ert-deftest remoteterm-tests-sink-flush-other-session ()
  "The end of an update flushes a session with no selected frame.
Redisplay flushes the display only for the selected frame."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let (((map :output :unflushed)
                 (rd-daemon-eval
                  (remoteterm-tests--frame-form
                   nil '(insert "abc")
                   '(let ((other (progn
                                   (x-open-connection "remote-other")
                                   (x-create-frame-with-faces
                                    '((display . "remote-other"))))))
                      (remote-display--record "remote-other")
                      (redraw-frame other)
                      (redisplay t)
                      (prog1
                          (list :output (remote-display--take-output
                                         "remote-other")
                                :unflushed (remote-display--unflushed-bytes
                                            "remote-other"))
                        (delete-frame other)))))))
      (should (> (length output) 0))
      (should (= unflushed 0)))))

(ert-deftest remoteterm-tests-sink-flush-display ()
  "A flush of the display gives a sink no reason to keep its bytes."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-sink"
    (remoteterm-tests--with-frame frame "remote-sink" nil
      (remote-display--record "remote-sink")
      (remoteterm-tests--sink-clear frame)
      (let ((size (length (remote-display--take-output "remote-sink"))))
        (should (> size 0))
        (should (= (remote-display--unflushed-bytes "remote-sink") size)))
      (remote-display--flush-display frame)
      (should (= (remote-display--unflushed-bytes "remote-sink") 0)))))

(ert-deftest remoteterm-tests-sink-replace ()
  "A new recording client does not get the bytes of the last client."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-sink"
    (remoteterm-tests--with-frame frame "remote-sink" nil
      (remote-display--record "remote-sink")
      (remoteterm-tests--sink-clear frame)
      (remote-display--record "remote-sink")
      (should (equal (remote-display--take-output "remote-sink") "")))))

(ert-deftest remoteterm-tests-sink-per-session ()
  "The messages of a session go only to the sink of that session."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-a"
    (remoteterm-tests--with-display "remote-b"
      (remoteterm-tests--with-frame frame "remote-a" nil
        (remote-display--record "remote-a")
        (remote-display--record "remote-b")
        (remoteterm-tests--sink-clear frame)
        (should (equal (remote-display--take-output "remote-b") ""))
        (should-not (equal (remote-display--take-output "remote-a") ""))))))

(defun remoteterm-tests--sink-area ()
  "Return random arguments for `remote-display--clear-area' after the frame."
  (list (rd-gen-int32) (rd-gen-int32)
        (rd-gen-integer 1 100) (rd-gen-integer 1 100)))

(ert-deftest remoteterm-tests-sink-kinds ()
  "A counting sink gets as many bytes as a recording sink gets."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-sink"
    (remoteterm-tests--with-frame frame "remote-sink" nil
      (let ((rd-gen-trials 50)
            (clear (lambda (areas)
                     (dolist (area areas)
                       (apply #'remote-display--clear-area frame area)))))
        (rd-gen-for-all
         (lambda () (rd-gen-list #'remoteterm-tests--sink-area 5))
         (lambda (areas)
           (remote-display--record "remote-sink")
           (funcall clear areas)
           (let ((recorded (remote-display--take-output "remote-sink")))
             (should-not (remote-display--counted-bytes "remote-sink"))
             (remote-display--count "remote-sink")
             (funcall clear areas)
             (should (= (remote-display--counted-bytes "remote-sink")
                        (length recorded)))
             (should-error (remote-display--take-output "remote-sink")))))))))

(defun remoteterm-tests--sink-operation ()
  "Return a random change to the sink of one of a few sessions."
  (list (rd-gen-element '(open close record count detach))
        (rd-gen-element '("remote-a" "remote-b"))))

(defun remoteterm-tests--apply-sink-operation (operation attached)
  "Apply OPERATION, and return the new value of the model ATTACHED.
ATTACHED is the list of the names of the sessions that have a sink."
  (pcase-exhaustive operation
    (`(open ,name) (x-open-connection name) attached)
    (`(close ,name)
     (when (member name (x-display-list))
       (x-close-connection name))
     (remove name attached))
    (`(record ,name)
     (remote-display--record name)
     (cons name (remove name attached)))
    (`(count ,name)
     (remote-display--count name)
     (cons name (remove name attached)))
    (`(detach ,name)
     (remote-display--detach name)
     (remove name attached))))

(ert-deftest remoteterm-tests-sink-lifetime ()
  "The live sinks are the sinks of the sessions, one for each session."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--close-all)
  (rd-gen-for-all
   (lambda () (rd-gen-list #'remoteterm-tests--sink-operation 8))
   (lambda (operations)
     (unwind-protect
         (let ((attached nil))
           (dolist (operation operations)
             (setq attached (remoteterm-tests--apply-sink-operation
                             operation attached))
             (should (= (remote-display--live-sinks) (length attached)))))
       (remoteterm-tests--close-all)
       (should (= (remote-display--live-sinks) 0))))))

;;;; Kboards

(defconst remoteterm-tests--kboard-cycles 20
  "The number of sessions of the tests of kboards.")

(defun remoteterm-tests--kboards-left (open-session)
  "Return the number of kboards of closed sessions that remain.
Call OPEN-SESSION to use the session \"remote-kboard\" for each of
`remoteterm-tests--kboard-cycles' sessions.  Each kboard holds a keymap
that nothing else holds, so a weak table shows if the kboard went away."
  (let ((keymaps (make-hash-table :test 'eq :weakness 'key)))
    (dotimes (_ remoteterm-tests--kboard-cycles)
      (remoteterm-tests--with-display "remote-kboard"
        (puthash (remote-display--kboard-keymap "remote-kboard") t keymaps)
        (funcall open-session)))
    (garbage-collect)
    (hash-table-count keymaps)))

(ert-deftest remoteterm-tests-close-frees-kboard ()
  "A closed session keeps no kboard."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--close-all)
  (should (< (remoteterm-tests--kboards-left #'ignore)
             remoteterm-tests--kboard-cycles)))

(ert-deftest remoteterm-tests-close-frees-kboard-of-frame ()
  "A session that had a frame frees its kboard too."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--close-all)
  (should (< (remoteterm-tests--kboards-left
              (lambda ()
                (remoteterm-tests--with-frame frame "remote-kboard" nil)))
             remoteterm-tests--kboard-cycles)))

;;;; Input

(defconst remoteterm-tests--function-keys
  '((#xff08 8 backspace) (#xff09 9 tab) (#xff0d 13 return)
    (#xff1b 27 escape) (#xffff 127 delete) (#xff50 0 home)
    (#xff51 0 left) (#xff52 0 up) (#xff53 0 right) (#xff54 0 down)
    (#xff55 0 prior) (#xff56 0 next) (#xff57 0 end) (#xff63 0 insert)
    (#xffbe 0 f1) (#xffc9 0 f12) (#xff95 0 kp-home) (#xffb1 0 kp-1))
  "Function keys: each element is (KEYSYM CHARACTER EVENT).
CHARACTER is the character that GDK gives for the key, or 0.  EVENT is
the event of Emacs for the key with no modifiers.")

(defun remoteterm-tests--random-key ()
  "Return a random key: (KEYSYM CHARACTER MODIFIERS EXPECTED).
MODIFIERS has the bits of `remote-display--modifier-bits'.  EXPECTED
is the event that Emacs gives for the key.  An ASCII keysym is its
character, a keysym from #x1000000 is #x1000000 plus a Unicode
character, and a Latin-1 keysym from #xa0 is its character.  The
event of a character has no shift modifier, because the keysym of a
shifted key is the shifted character, but shift can change the case
in a key chord: see `remoteterm-tests--chord-case'.  The space keeps
shift, so S-SPC is not SPC."
  (let* ((modifiers (rd-gen-integer 0 63))
         (names (cl-loop for (name . bit) in remote-display--modifier-bits
                         unless (zerop (logand modifiers bit))
                         collect name)))
    (pcase (rd-gen-integer 0 3)
      (0 (let ((key (rd-gen-element remoteterm-tests--function-keys)))
           (list (nth 0 key) (nth 1 key) modifiers
                 (event-convert-list (append names (list (nth 2 key)))))))
      (kind
       (let* ((character (pcase kind
                           (1 (rd-gen-integer 32 126))
                           (2 (rd-gen-integer #xa0 #xff))
                           (_ (rd-gen-integer #x100 #x2fff))))
              ;; C-g quits.  The test of quit has its own case.
              (character (if (memq character '(?g ?G)) ?h character))
              (keysym (if (< character #x100) character
                        (+ #x1000000 character))))
         (list keysym character modifiers
               (event-convert-list
                (append (if (= character ?\s) names
                          (delq 'shift (copy-sequence names)))
                        (list (remoteterm-tests--chord-case character
                                                            names))))))))))

(defun remoteterm-tests--chord-case (character modifiers)
  "Return CHARACTER with the case that a key chord with MODIFIERS gives.
In a chord with a modifier other than shift, keyboard.c takes a
capital letter with no shift, or a small letter with shift, as the
effect of Caps Lock, and changes the case.  This is the rule of
`make_lispy_event'."
  (let ((shift (memq 'shift modifiers))
        (upper (/= character (downcase character)))
        (lower (and (= character (downcase character))
                    (/= character (upcase character)))))
    (cond ((null (delq 'shift (copy-sequence modifiers))) character)
          ((and upper (not shift)) (downcase character))
          ((and lower shift) (upcase character))
          (t character))))

;; An input of the property of the key messages is (MESSAGE EVENT):
;; a message from the client, and the event that it gives, or nil if it
;; gives none.  The message can also be the bytes of a frame of a type
;; that the session does not know.  The frame field of a message is
;; `frame', for the frame of the test, or `other', for a frame that does
;; not exist.  The daemon puts the IDs of the frames in their place.

(defconst remoteterm-tests--modifier-keysyms
  '(#xffe1 #xffe2 #xffe3 #xffe5 #xffe9 #xffeb #xffee #xfe01 #xfe03 #xfe0f)
  "Keysyms of modifier keys and lock keys, which give no event.
The list has the first and the last keysym of each range.")

(defconst remoteterm-tests--vendor-keysyms
  '(#x1008ff11 #x1008ff13 #x10000000)
  "Vendor keysyms.  The event of such a key is the symbol key-N, with N
the keysym, because the session has no `system-key-alist'.")

(defun remoteterm-tests--random-input ()
  "Return a random input of the property of the key messages."
  (pcase (rd-gen-integer 0 15)
    (14 (list (list 'close-frame :frame 'other) nil))
    (11 (let ((keysym (rd-gen-element remoteterm-tests--vendor-keysyms))
              (modifiers (rd-gen-integer 0 63)))
          (list (list 'key :frame 'frame :time 0 :keysym keysym
                      :character 0 :modifiers modifiers)
                (event-convert-list
                 (append (remoteterm-tests--modifier-names modifiers)
                         (list (intern (format "key-%d" keysym))))))))
    (12 (list (list 'key :frame 'frame :time 0
                    :keysym (rd-gen-integer #x100 #xfdff)
                    :character (rd-gen-element (list 0 #x400000 #xffffffff))
                    :modifiers 0)
              nil))
    (13 (list (remoteterm-tests--random-pointer-message) nil))
    (0 (list (list 'key :frame 'frame :time 0
                   :keysym (rd-gen-element
                            remoteterm-tests--modifier-keysyms)
                   :character 0 :modifiers (rd-gen-integer 0 63))
             nil))
    (1 (let ((key (remoteterm-tests--random-key)))
         (list (remoteterm-tests--key-message 'other key) nil)))
    (2 (list (list 'hello :version (rd-gen-uint16)) nil))
    (3 (list (remoteterm-tests--unknown-type-frame) nil))
    (_ (let ((key (remoteterm-tests--random-key)))
         (list (remoteterm-tests--key-message 'frame key) (nth 3 key))))))

(defun remoteterm-tests--modifier-names (modifiers)
  "Return the names of the bits of MODIFIERS."
  (cl-loop for (name . bit) in remote-display--modifier-bits
           unless (zerop (logand modifiers bit))
           collect name))

(defun remoteterm-tests--random-pointer-message ()
  "Return a random message that gives no event: a motion message, or a
focus or a resize message, which the session does not use yet."
  (pcase (rd-gen-integer 0 2)
    (0 (list 'motion :frame 'frame :time 0 :x (rd-gen-int32)
             :y (rd-gen-int32)))
    (1 (list 'focus :frame 'frame :in (rd-gen-integer 0 1)))
    (_ (list 'resize :frame 'frame :width (rd-gen-uint32)
             :height (rd-gen-uint32)))))

(defun remoteterm-tests--unknown-type-frame ()
  "Return the bytes of a frame of a type that the session does not know."
  (let ((body (rd-gen-bytes (rd-gen-integer 0 40))))
    (concat (unibyte-string 0 0 0 (1+ (length body)) 255) body)))

(defun remoteterm-tests--key-message (frame key)
  "Return the key message of KEY, a `remoteterm-tests--random-key', for FRAME."
  (list 'key :frame frame :time 0 :keysym (nth 0 key)
        :character (nth 1 key) :modifiers (nth 2 key)))

(defun remoteterm-tests--random-inputs ()
  "Return random inputs and chunks: (INPUTS CHUNKS).
CHUNKS is a list of the sizes of the parts of the bytes of INPUTS that
the client sends one at a time."
  (list (cl-loop repeat (rd-gen-integer 1 20)
                 collect (remoteterm-tests--random-input))
        (cl-loop repeat (rd-gen-integer 0 8)
                 collect (rd-gen-integer 1 30))))

(defun remoteterm-tests--read-events (count)
  "Read up to COUNT events, and return them in order.
Leave out each `switch-frame' event.  Stop after 10 seconds, or after
one second with no event."
  (let ((events nil)
        (idle 0))
    (with-timeout (10)
      (while (and (< (length events) count) (< idle 20))
        (accept-process-output nil 0.05)
        (let ((event (read-event nil nil 0.01)))
          (cond ((null event) (cl-incf idle))
                ((not (eq (car-safe event) 'switch-frame))
                 (setq idle 0)
                 (push event events))))))
    (nreverse events)))

(defun remoteterm-tests--send-in-chunks (process bytes chunks)
  "Send BYTES to PROCESS in parts of the sizes CHUNKS, and then the rest.
Read between the parts, so that the other end gets them apart."
  (dolist (size chunks)
    (let ((part (substring bytes 0 (min size (length bytes)))))
      (setq bytes (substring bytes (length part)))
      (process-send-string process part)
      (accept-process-output nil 0.01)))
  (process-send-string process bytes))

(defun remoteterm-tests--with-input-session (function)
  "Call FUNCTION in a session with a frame and an attached client.
The arguments of FUNCTION are the frame, the client side of the
connection, and the server side.  Return the value of FUNCTION."
  (remoteterm-tests--with-display "remote-input"
    (remoteterm-tests--with-frame frame "remote-input" nil
      (select-frame frame)
      (remoteterm-tests--with-connections
       '(a)
       (lambda (connections)
         (let ((connection (alist-get 'a connections)))
           (remote-display-attach "remote-input"
                                  (plist-get connection :server-side))
           (funcall function frame (plist-get connection :client-side)
                    (plist-get connection :server-side))))))))

(defun remoteterm-tests--encode-inputs (inputs frame)
  "Return the bytes of the messages of INPUTS for FRAME."
  (let ((id (remoteterm-tests--frame-id frame)))
    (mapconcat
     (lambda (input)
       (if (stringp (car input))
           (car input)
       (let ((message (copy-sequence (car input))))
         (when (plist-member (cdr message) :frame)
           (setq message
                 (cons (car message)
                       (plist-put (cdr message) :frame
                                  (if (eq (plist-get (cdr message) :frame)
                                          'frame)
                                      id
                                    (+ id 1000))))))
         (remote-display--encode message))))
     inputs)))

(defun remoteterm-tests--run-inputs (case)
  "Send the inputs of CASE, a `remoteterm-tests--random-inputs', to a session.
Return the events of the session."
  (remoteterm-tests--with-input-session
   (lambda (frame client-side &optional _server-side)
     (remoteterm-tests--send-in-chunks
      client-side (remoteterm-tests--encode-inputs (car case) frame)
      (cadr case))
     (remoteterm-tests--read-events (length (car case))))))

(rd-daemon-deftest remoteterm-tests-key-events
  "A key message gives the event of Emacs for its key and modifiers.
A function key gives the event of its name, with each modifier.  A
character gives the character, with each modifier but shift.  A
modifier key, a key for a frame that does not exist, and a message of
another type give no event.  The chunks of the bytes do not change
the events."
  (let ((rd-gen-trials 30))
    (rd-gen-for-all
     #'remoteterm-tests--random-inputs
     (lambda (case)
       (should (equal (remoteterm-tests--run-inputs case)
                      (delq nil (mapcar #'cadr (car case)))))))))

;; The examples run in a daemon too, but they have no trials, so they
;; do not use `rd-daemon-deftest'.

(defun remoteterm-tests--daemon-call (function)
  "Call FUNCTION in the daemon of `rd-with-daemon', and return its value.
The daemon loads the file of FUNCTION first."
  (rd-daemon-eval
   `(let ((load-prefer-newer t))
      (load ,(file-name-sans-extension (symbol-file function 'defun)) nil t)
      (,function))))

(defun remoteterm-tests--input-error-case ()
  "Send a key and a frame with an error, then a key, and return the events.
The last key comes in its own write, after the session read the error,
so the input of the session is empty when it comes."
  (remoteterm-tests--with-input-session
   (lambda (frame client-side &optional _server-side)
     (let ((key (remoteterm-tests--encode-inputs
                 '(((key :frame frame :time 0 :keysym ?a :character ?a
                         :modifiers 0)))
                 frame)))
       (process-send-string client-side
                            (concat key (unibyte-string 0 0 0 0)))
       (let ((first (remoteterm-tests--read-events 1)))
         (process-send-string client-side key)
         (append first (remoteterm-tests--read-events 1)))))))

(ert-deftest remoteterm-tests-input-error ()
  "After a message with an error, the session ignores the input."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--input-error-case)
                   '(?a)))))

(defun remoteterm-tests--input-quit-case ()
  "Send the key C-g, run a loop, and return `quit' if the loop quits."
  (remoteterm-tests--with-input-session
   (lambda (frame client-side &optional _server-side)
     (process-send-string
      client-side
      (remoteterm-tests--encode-inputs
       '(((key :frame frame :time 0 :keysym ?g :character ?g
               :modifiers 2)))
       frame))
     (condition-case nil
         (with-timeout (5 'timeout)
           (while t (accept-process-output nil 0.01)))
       (quit 'quit)))))

(ert-deftest remoteterm-tests-input-quit ()
  "The key C-g from the client quits a running command."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (eq (remoteterm-tests--daemon-call
                 'remoteterm-tests--input-quit-case)
                'quit))))

(defun remoteterm-tests--close-frame-case ()
  "Send close-frame messages to a session with two frames.
Return a list with an element for each step:
 - The event of a message for the first frame, with an empty
   `special-event-map', so that `read-event' gives the event.
 - The state after a message for a frame that does not exist.
 - The state after a message for the second frame.
 - The state after a message for the first frame, the last one.
A state is the list of the frames that are live, and whether the
session is live.  The frames are `first' and `second' in the list.
The daemon is live if the function returns."
  (remoteterm-tests--with-input-session
   (lambda (first client-side &optional _server-side)
     (let ((second (remoteterm-tests--make-frame "remote-input" nil)))
       (unwind-protect
           (cl-flet ((send (frame target)
                       (process-send-string
                        client-side
                        (remoteterm-tests--encode-inputs
                         `(((close-frame :frame ,target))) frame))
                       (remoteterm-tests--read-events 1))
                     (state ()
                       (list (seq-filter #'frame-live-p (list first second))
                             (and (remoteterm-tests--terminal "remote-input")
                                  t))))
             (cl-subst-if
              'first (lambda (x) (eq x first))
              (cl-subst-if
               'second (lambda (x) (eq x second))
               (list (let ((special-event-map (make-sparse-keymap)))
                       (send first 'frame))
                     (progn (send first 'other) (state))
                     (progn (send second 'frame) (state))
                     (progn (send first 'frame) (state))))))
         (delete-frame second))))))

(ert-deftest remoteterm-tests-close-frame ()
  "A close-frame message gives the delete-frame event of its frame.
The event deletes the frame, as the delete-window event of X does.  A
message for a frame that does not exist gives no event.  A message for
the last frame of the session deletes the frame and the session, as
`delete-frame' does, but does not stop the daemon."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--close-frame-case)
                   '(((delete-frame (first)))
                     ((first second) t)
                     ((first) t)
                     (nil nil))))))

(defun remoteterm-tests--input-after-quit-case ()
  "Send C-g, a, and b in one write, and read events as an idle Emacs does.
Read with `read-event' only, so that Emacs waits in read_char.  Return
the events, with `quit' for a quit."
  (remoteterm-tests--with-input-session
   (lambda (frame client-side &optional _server-side)
     (process-send-string
      client-side
      (remoteterm-tests--encode-inputs
       '(((key :frame frame :time 0 :keysym ?g :character ?g :modifiers 2))
         ((key :frame frame :time 0 :keysym ?a :character ?a :modifiers 0))
         ((key :frame frame :time 0 :keysym ?b :character ?b :modifiers 0)))
       frame))
     (let ((events nil))
       (with-timeout (5)
         (while (not (and (memq ?a events) (memq ?b events)))
           (let ((event (condition-case nil (read-event nil nil 0.1)
                          (quit 'quit))))
             (when (and event (not (eq (car-safe event) 'switch-frame)))
               (push event events)))))
       (nreverse events)))))

(ert-deftest remoteterm-tests-input-after-quit ()
  "The keys after C-g in one chunk come too, with no more input."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (let ((events (remoteterm-tests--daemon-call
                   'remoteterm-tests--input-after-quit-case)))
      (should (memq ?a events))
      (should (memq ?b events)))))

(defun remoteterm-tests--not-client-case ()
  "Give bytes to `remote-display--receive' for a process with no session.
Return t if nothing failed."
  (remoteterm-tests--with-connections
   '(a)
   (lambda (connections)
     (remote-display--receive (plist-get (alist-get 'a connections)
                                         :server-side)
                              (unibyte-string 0 0 0 0))
     t)))

(ert-deftest remoteterm-tests-input-not-client ()
  "Bytes for a process that is not the client of a session do nothing."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (eq (remoteterm-tests--daemon-call
                 'remoteterm-tests--not-client-case)
                t))))

(defun remoteterm-tests--input-reattach-case ()
  "Attach a client, detach it, attach it again, and return its key events."
  (remoteterm-tests--with-input-session
   (lambda (frame client-side server-side)
     (remote-display--detach "remote-input")
     (remote-display-attach "remote-input" server-side)
     (process-send-string
      client-side
      (remoteterm-tests--encode-inputs
       '(((key :frame frame :time 0 :keysym ?z :character ?z :modifiers 0)))
       frame))
     (remoteterm-tests--read-events 1))))

(ert-deftest remoteterm-tests-input-reattach ()
  "A client that attaches again gives its input to the session."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--input-reattach-case)
                   '(?z)))))

(rd-daemon-deftest remoteterm-tests-listen-attaches-newest
  "A connection to the socket of `remote-display-listen' becomes the client.
Each new connection replaces the client before it: a ping on the
newest connection gets a pong, and a ping on an older live connection
does not.  An older connection can close first.  The directory of the
socket has the mode 700.  After the session closes, the socket and its
directory do not exist, and a new session with the same name can
listen."
  (let ((rd-gen-trials 30))
    (rd-gen-for-all
     (lambda ()
       (cl-loop repeat (rd-gen-integer 1 4) collect (rd-gen-random 2)))
     (lambda (closes)
       (let ((result (remoteterm-tests--listen-case closes)))
         (should (equal (plist-get result :pongs)
                        (cl-loop for i from 1 to (length closes)
                                 collect (list i nil))))
         (should (= (plist-get result :mode) #o700))
         (should-not (plist-get result :left-after-close)))))))

(defun remoteterm-tests--listen-case (closes)
  "Connect to the socket of a listening session, once for each of CLOSES.
An element of CLOSES that is 1 closes the newest connection before the
new connection.  Return a plist.  :pongs has (SERIAL OLD-PONGS) for
each connection: SERIAL is the serial of the pong of the new
connection, and OLD-PONGS is the list of the pongs on the older live
connections.  The older connections get their pings after the pong of
the new connection, because that pong shows that the new connection
is attached.  :mode is the mode of the directory of the socket.
:left-after-close lists what is left after the session closes: the
socket, its directory, or an error of a new listen with the same
name."
  (let ((socket nil) (mode nil) (pongs nil) (live nil) (all nil))
    (unwind-protect
        (progn
          (remoteterm-tests--with-display "remote-listen"
            (setq socket (remote-display-listen "remote-listen"))
            (setq mode (file-modes (file-name-directory socket)))
            (cl-loop
             for close in closes for serial from 1 do
             (when (and live (= close 1))
               (delete-process (car (pop live))))
             (let ((new (remoteterm-tests--connect socket)))
               (push new all)
               (remoteterm-tests--reset-and-ping new serial)
               (let ((pong (remoteterm-tests--pong-of (car new) (cdr new))))
                 (dolist (old live)
                   (remoteterm-tests--reset-and-ping old 100))
                 (when live
                   (accept-process-output nil 0.1))
                 (push (list pong
                             (delq nil (mapcar (lambda (old)
                                                 (remoteterm-tests--read-pong
                                                  (cdr old)))
                                               live)))
                       pongs))
               (push new live))))
          (list :pongs (nreverse pongs) :mode mode
                :left-after-close
                (delq nil (list (file-exists-p socket)
                                (file-exists-p (file-name-directory socket))
                                (remoteterm-tests--listen-error
                                 "remote-listen")))))
      (dolist (connection all)
        (delete-process (car connection))))))

(defun remoteterm-tests--connect (socket)
  "Connect to SOCKET, and return (PROCESS . RECEIVED).
RECEIVED is a list whose car has the strings that PROCESS got."
  (let ((received (list nil)))
    (cons (make-network-process
           :name "rd-listen" :family 'local :service socket :coding 'binary
           :filter (lambda (_process string)
                     (push string (car received))))
          received)))

(defun remoteterm-tests--listen-error (display)
  "Open DISPLAY, listen, and close it.  Return the error, or nil."
  (condition-case err
      (remoteterm-tests--with-display display
        (remote-display-listen display)
        nil)
    (error err)))

(defun remoteterm-tests--listen-socket-works-p (socket)
  "Return non-nil if a ping on a new connection to SOCKET gets its pong."
  (let ((connection (remoteterm-tests--connect socket)))
    (unwind-protect
        (progn
          (remoteterm-tests--reset-and-ping connection 7)
          (eql (remoteterm-tests--pong-of (car connection) (cdr connection))
               7))
      (delete-process (car connection)))))

(ert-deftest remoteterm-tests-listen-twice ()
  "A second socket for one session is an error, and the first one works."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--listen-twice-case)
                   '(t t)))))

(defun remoteterm-tests--listen-twice-case ()
  "Listen two times on one session.
Return (ERROR WORKS): ERROR is t if the second listen signals an
error, and WORKS is t if the first socket still works."
  (remoteterm-tests--with-display "remote-listen-a"
    (let* ((socket (remote-display-listen "remote-listen-a"))
           (error (condition-case nil
                      (progn (remote-display-listen "remote-listen-a") nil)
                    (error t))))
      (list error (and (remoteterm-tests--listen-socket-works-p socket) t)))))

(ert-deftest remoteterm-tests-listen-with-frames ()
  "A session with frames does not close, and keeps its socket."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (remoteterm-tests--daemon-call
             'remoteterm-tests--listen-with-frames-case))))

(defun remoteterm-tests--listen-with-frames-case ()
  "Return t if the socket of a session with a frame works after a close."
  (remoteterm-tests--with-display "remote-listen-a"
    (let ((socket (remote-display-listen "remote-listen-a")))
      (remoteterm-tests--with-frame _frame "remote-listen-a" nil
        (ignore-errors (x-close-connection "remote-listen-a"))
        (and (remoteterm-tests--listen-socket-works-p socket) t)))))

;;;; Clipboard

(defun remoteterm-tests--random-text ()
  "Return a random text for the clipboard, with some non-ASCII characters.
Some texts are empty, and some need more than one clipboard-data
message.  In some texts, a character of 2 to 4 UTF-8 bytes crosses the
border of the first two messages."
  (if (zerop (rd-gen-integer 0 4))
      (let* ((char (rd-gen-element '(?é ?€ ?😀)))
             (length (string-bytes (string char))))
        ;; The character starts 1 to LENGTH - 1 bytes before the
        ;; border, so it crosses the border.
        (concat (make-string (- remote-display--max-clipboard-chunk
                                (rd-gen-integer 1 (1- length)))
                             ?a)
                (string char)
                "\r\0end"))
    (let ((length (rd-gen-element (list 0 1 (rd-gen-integer 1 100)
                                        (rd-gen-integer 2700 9000)))))
      (apply #'string
             (cl-loop repeat length
                      collect (rd-gen-element
                               '(?a ?z ?\n ?\r ?\0 ?\s ?é ?λ ?€ ?😀)))))))

(defun remoteterm-tests--clipboard-bytes (serial text)
  "Return the clipboard-data messages of TEXT with SERIAL, encoded."
  (let* ((bytes (encode-coding-string text 'utf-8-unix))
         (chunk remote-display--max-clipboard-chunk)
         (offset 0)
         (messages nil))
    (while (progn
             (let ((end (min (length bytes) (+ offset chunk))))
               (push (remote-display--encode
                      `(clipboard-data :serial ,serial :offset ,offset
                                       :total ,(length bytes)
                                       :bytes ,(vconcat
                                                (substring bytes offset end))))
                     messages)
               (setq offset end))
             (< offset (length bytes))))
    (apply #'concat (nreverse messages))))

(defun remoteterm-tests--joined-clipboard (bytes)
  "Return the text of the clipboard-data messages in BYTES, or nil.
The messages must have one serial and one total, and each one must
start where the one before it ends.  Else return the symbol `broken'."
  (let ((data (mapcar #'cdr
                      (seq-filter (lambda (message)
                                    (eq (car message) 'clipboard-data))
                                  (remoteterm-tests--all-messages bytes))))
        (offset 0))
    (cond
     ((null data) nil)
     ((not (cl-every (lambda (fields)
                       (prog1 (and (= (plist-get fields :serial)
                                      (plist-get (car data) :serial))
                                   (= (plist-get fields :total)
                                      (plist-get (car data) :total))
                                   (= (plist-get fields :offset) offset))
                         (setq offset (+ offset (length (plist-get fields
                                                                   :bytes))))))
                     data))
      'broken)
     ((/= offset (plist-get (car data) :total)) 'broken)
     (t (decode-coding-string
         (apply #'unibyte-string
                (seq-mapcat (lambda (fields)
                              (append (plist-get fields :bytes) nil))
                            data))
         'utf-8-unix)))))

(rd-daemon-deftest remoteterm-tests-clipboard-from-client
  "The clipboard text of the client becomes the CLIPBOARD selection (law 42)."
  (let ((rd-gen-trials 8))
    (rd-gen-for-all
     #'remoteterm-tests--random-text
     (lambda (text)
       (should (equal (remoteterm-tests--clipboard-from-client-case text)
                      (list text nil nil)))))))

(defun remoteterm-tests--clipboard-from-client-case (text)
  "Kill a text, send TEXT as the clipboard of a client, and return a list.
The value is (VALUE OWNER TARGETS): the CLIPBOARD selection as
UTF8_STRING, whether Emacs owns CLIPBOARD, and the selection as
TARGETS."
  (remoteterm-tests--with-input-session
   (lambda (_frame client-side &optional _server-side)
     (gui-set-selection 'CLIPBOARD "killed before")
     (process-send-string client-side
                          (remoteterm-tests--clipboard-bytes 7 text))
     (let ((end (+ (float-time) 10))
           (value nil))
       (while (and (not (equal (setq value (gui-get-selection
                                            'CLIPBOARD 'UTF8_STRING))
                               text))
                   (< (float-time) end))
         (accept-process-output nil 0.05))
       (list value (and (gui-backend-selection-owner-p 'CLIPBOARD) t)
             (gui-get-selection 'CLIPBOARD 'TARGETS))))))

(rd-daemon-deftest remoteterm-tests-clipboard-to-client
  "A kill sends the text to the client (law 43)."
  (let ((rd-gen-trials 8))
    (rd-gen-for-all
     #'remoteterm-tests--random-text
     (lambda (text)
       (should (equal (remoteterm-tests--clipboard-to-client-case text)
                      (list text t text nil)))))))

(defun remoteterm-tests--clipboard-to-client-case (text)
  "Set CLIPBOARD and PRIMARY to TEXT in a session, and return a list.
The list is (SENT OWNER VALUE PRIMARY-SENT).  SENT is the text of the
clipboard-data messages of the client after the CLIPBOARD selection.
OWNER is t if Emacs owns CLIPBOARD.  VALUE is the CLIPBOARD selection.
PRIMARY-SENT is non-nil if the selection of PRIMARY sent a
clipboard-data message."
  (remoteterm-tests--with-input-session
   (lambda (_frame client-side &optional _server-side)
     (let ((chunks nil))
       (set-process-filter client-side
                           (lambda (_process string) (push string chunks)))
       (gui-set-selection 'CLIPBOARD text)
       (let ((end (+ (float-time) 10)) (sent nil))
         (while (and (not (equal (setq sent
                                       (remoteterm-tests--joined-clipboard
                                        (string-to-unibyte
                                         (apply #'concat (reverse chunks)))))
                                 text))
                     (< (float-time) end))
           (accept-process-output nil 0.05))
         (setq chunks nil)
         (gui-set-selection 'PRIMARY text)
         (accept-process-output nil 0.2)
         (list sent
               (and (gui-backend-selection-owner-p 'CLIPBOARD) t)
               (gui-get-selection 'CLIPBOARD 'UTF8_STRING)
               (and (remoteterm-tests--joined-clipboard
                     (string-to-unibyte (apply #'concat (reverse chunks))))
                    t)))))))

(ert-deftest remoteterm-tests-clipboard-pace ()
  "Clipboard data waits while the sink of the session is full (law 44)."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let (((map :full :sent)
                 (remoteterm-tests--daemon-call
                  'remoteterm-tests--clipboard-pace-case)))
      (should (equal full ""))
      (should (equal sent (make-string 20000 ?y))))))

(defun remoteterm-tests--clipboard-pace-case ()
  "Kill two texts while the sink of the session is full.
The value of the form of `remoteterm-tests--clipboard-pace-form'."
  (eval (remoteterm-tests--clipboard-pace-form) t))

(defun remoteterm-tests--clipboard-pace-form ()
  "Return a form that kills two texts with a full sink, and empties it.
The value of the form is a plist.  :full is the text of the clipboard
data that the session sends while the sink reports 65536 bytes, or the
empty string.  :sent is the text of the clipboard data after the sink
reports 65535 bytes and an empty queue."
  (remoteterm-tests--frame-form
   '((width . 30))
   '(insert "abc")
   '(let ((text-of (lambda ()
                     (or (remoteterm-tests--joined-clipboard
                          (remote-display--take-output display))
                         ""))))
      (remote-display--set-queued display 65536)
      (gui-set-selection 'CLIPBOARD (make-string 20000 ?x))
      (gui-set-selection 'CLIPBOARD (make-string 20000 ?y))
      (list :full (funcall text-of)
            :sent (progn (remote-display--set-queued display 65535)
                         (remote-display--queue-empty display)
                         (funcall text-of))))))

(ert-deftest remoteterm-tests-clipboard-no-client ()
  "A session with no client keeps its selections (law 43)."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--clipboard-no-client-case)
                   '("kill é" "mouse" t t nil nil t
                     remote-display-clipboard-too-large)))))

(defun remoteterm-tests--clipboard-no-client-case ()
  "Set CLIPBOARD and PRIMARY in a session with no client.
Return a list: the two selections, whether PRIMARY exists and Emacs
owns it, whether SECONDARY exists and Emacs owns it, whether a kill of
a text above the limit keeps the text, and the error symbol of a send
of that text to the client."
  (remoteterm-tests--with-display "remote-clip"
    (remoteterm-tests--with-frame frame "remote-clip" nil
      (select-frame frame)
      (gui-set-selection 'CLIPBOARD "kill é")
      (gui-set-selection 'PRIMARY "mouse")
      (list (gui-get-selection 'CLIPBOARD 'UTF8_STRING)
            (gui-get-selection 'PRIMARY 'UTF8_STRING)
            (gui-backend-selection-exists-p 'PRIMARY)
            (gui-backend-selection-owner-p 'PRIMARY)
            (gui-backend-selection-exists-p 'SECONDARY)
            (gui-backend-selection-owner-p 'SECONDARY)
            (let ((large (make-string (1+ (* 16 1024 1024)) ?a)))
              (gui-set-selection 'CLIPBOARD large)
              (eq (gui-get-selection 'CLIPBOARD 'UTF8_STRING) large))
            (condition-case err
                (remote-display--send-clipboard
                 "remote-clip" (make-string (1+ (* 16 1024 1024)) ?a))
              (error (car err)))))))

(ert-deftest remoteterm-tests-clipboard-new-client ()
  "A new client drops the rest of a clipboard text (law 43)."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--clipboard-new-client-case)
                   "before"))))

(defun remoteterm-tests--clipboard-new-client-case ()
  "Send the first message of a text, attach again, and send the rest.
Return the CLIPBOARD selection, which was \"before\" before the text."
  (remoteterm-tests--with-input-session
   (lambda (_frame client-side &optional server-side)
     (process-send-string client-side
                          (remoteterm-tests--clipboard-bytes 3 "before"))
     (remoteterm-tests--await-selection "before")
     (let* ((text (make-string 20000 ?z))
            (bytes (remoteterm-tests--clipboard-bytes 4 text))
            (first (cdr (remote-display--decode bytes)))
            (rest (substring bytes first)))
       (process-send-string client-side (substring bytes 0 first))
       (accept-process-output nil 0.2)
       (remote-display--detach "remote-input")
       (remote-display-attach "remote-input" server-side)
       (process-send-string client-side rest)
       (accept-process-output nil 0.5)
       (gui-get-selection 'CLIPBOARD 'UTF8_STRING)))))

(defun remoteterm-tests--await-selection (text)
  "Wait at most 10 seconds for the CLIPBOARD selection to be TEXT."
  (let ((end (+ (float-time) 10)))
    (while (and (not (equal (gui-get-selection 'CLIPBOARD 'UTF8_STRING)
                            text))
                (< (float-time) end))
      (accept-process-output nil 0.05))))

;;;; Popup menus

(defvar remote-display--max-menu-text)
(defvar remote-display--max-menu-key)

(ert-deftest remoteterm-tests-menu-entries ()
  "Law M3: the messages of a popup menu have the entries of the menu.
A trial pops up a random menu of `rd-gen-menu' at a random position,
relative to a frame or to its window.  A timer takes the output of the
session while `x-popup-menu' waits, and answers with a dismissal.  The
model of the entries is `remoteterm-tests--menu-entries'."
  (skip-unless (featurep 'remote-test))
  (let ((rd-gen-trials 40))
    (rd-gen-for-all
     (lambda () (list (rd-gen-menu) (rd-gen-element '(frame window))
                      (rd-gen-random 300) (rd-gen-random 300)))
     (lambda (case)
       (remoteterm-tests--check-menu-entries
        (car case) (apply #'remoteterm-tests--popup-menu case))))))

(ert-deftest remoteterm-tests-menu-empty ()
  "A popup menu with no entries signals \"Empty menu\"."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-menu"
    (remoteterm-tests--with-frame frame "remote-menu" nil
      (remote-display--record "remote-menu")
      (should (equal (should-error (x-popup-menu (list '(0 0) frame)
                                                 '(keymap)))
                     '(error "Empty menu")))
      (should (equal (remote-display--take-output "remote-menu") "")))))

(defun remoteterm-tests--check-menu-entries (menu result)
  "Check law M3 for MENU and RESULT, a value of `remoteterm-tests--popup-menu'."
  (pcase-let* (((map :answered :unflushed :messages :frame :position) result)
               (menu-messages (seq-filter
                               (lambda (message)
                                 (memq (car message) '(menu-item show-menu)))
                               messages))
               (show (cdr (car (last menu-messages))))
               (items (mapcar #'cdr (butlast menu-messages))))
    (should answered)
    (should (eql unflushed 0))
    (should (equal (mapcar #'car menu-messages)
                   (append (make-list (length items) 'menu-item)
                           '(show-menu))))
    (should (equal (list (plist-get show :frame) (plist-get show :x)
                         (plist-get show :y))
                   (cons frame position)))
    (should (equal (remoteterm-tests--menu-bytes-text (plist-get show :title))
                   (remoteterm-tests--cut-text
                    (remoteterm-tests--menu-title menu)
                    remote-display--max-menu-text)))
    (should (cl-every (lambda (item)
                        (eql (plist-get item :serial) (plist-get show :serial)))
                      items))
    (should (remoteterm-tests--well-nested-p
             (mapcar (lambda (item) (plist-get item :kind)) items)))
    (should (equal (mapcar #'remoteterm-tests--menu-item-entry items)
                   (remoteterm-tests--menu-entries menu)))
    (remoteterm-tests--check-menu-ids items)))

(defun remoteterm-tests--check-menu-ids (items)
  "Check that the ids of ITEMS of the item kind are all different.
The other kinds have the id 0."
  (let ((ids (mapcar (lambda (item) (plist-get item :id))
                     (seq-filter (lambda (item) (eql (plist-get item :kind) 0))
                                 items))))
    (should (equal (seq-uniq ids) ids))
    (should (cl-every (lambda (item) (eql (plist-get item :id) 0))
                      (seq-remove (lambda (item)
                                    (eql (plist-get item :kind) 0))
                                  items)))))

(defun remoteterm-tests--popup-menu (menu where x y)
  "Pop up MENU at X, Y on a remote frame, and return what the client got.
WHERE is `frame' or `window': X and Y are relative to the frame or to
its root window.  The frame has a menu bar, so the window is below the
top of the frame.  A timer takes the output of the session while
`x-popup-menu' waits, and answers with a dismissal for the serial of
the show-menu message.  The position is not a click, so the dismissal
signals `quit'.  Another timer ends the wait after 5 seconds, so a
failure does not stop the test.  The value is a plist.  :answered is
non-nil if the first timer ran before `x-popup-menu' returned.
:messages are the decoded messages before the answer, and :unflushed
is the number of bytes that the session did not flush then.  :frame is
the ID of the frame, and :position is the list (X Y) of the expected
pixel position in the frame."
  (remoteterm-tests--with-display "remote-menu"
    (remoteterm-tests--with-frame frame "remote-menu" '((menu-bar-lines . 1))
      (remote-display--record "remote-menu")
      (let* ((window (frame-root-window frame))
             (state (list :frame (remoteterm-tests--frame-id frame)
                          :position (remoteterm-tests--menu-position
                                     where window x y)))
             (answer (run-at-time 0 nil
                                  (lambda ()
                                    (setq state (remoteterm-tests--answer-menu
                                                 "remote-menu" state)))))
             (guard (remoteterm-tests--menu-guard)))
        (unwind-protect
            (remoteterm-tests--catch-menu-exit
             (x-popup-menu (list (list x y) (if (eq where 'frame) frame window))
                           menu))
          (cancel-timer answer)
          (cancel-timer guard))
        state))))

(defun remoteterm-tests--menu-guard ()
  "Return a timer that ends the wait of a popup menu after 5 seconds.
The timer throws `hang' to `remoteterm-tests--catch-menu-exit', so a
failure does not stop the test."
  (run-at-time 5 nil #'throw 'remoteterm-tests--menu-exit 'hang))

(defmacro remoteterm-tests--catch-menu-exit (&rest body)
  "Evaluate BODY, and return its value or the cause of its exit.
The value is `quit' if BODY signals `quit', and the value of a throw to
`remoteterm-tests--menu-exit' if BODY throws there."
  `(catch 'remoteterm-tests--menu-exit
     (condition-case nil
         (progn ,@body)
       (quit 'quit))))

(defun remoteterm-tests--menu-position (where window x y)
  "Return the pixel position (X Y) in the frame of WINDOW.
WHERE is `frame' if X and Y are relative to the frame, and `window'
if they are relative to WINDOW."
  (if (eq where 'frame)
      (list x y)
    (pcase-let ((`(,left ,top . ,_) (window-pixel-edges window)))
      (list (+ left x) (+ top y)))))

(defun remoteterm-tests--answer-menu (display state)
  "Take the output of DISPLAY, and answer its menu with a dismissal.
Return STATE with :answered, :unflushed, and :messages (see
`remoteterm-tests--popup-menu')."
  (let* ((unflushed (remote-display--unflushed-bytes display))
         (messages (remoteterm-tests--all-messages
                    (remote-display--take-output display)))
         (show (alist-get 'show-menu messages)))
    (remote-display--input display
                           (remote-display--encode
                            `(menu-choice :serial ,(or (plist-get show :serial)
                                                       0)
                                          :chosen 0 :id 0)))
    (append (list :answered t :unflushed unflushed :messages messages)
            state)))

(defun remoteterm-tests--menu-item-entry (item)
  "Return the entry (KIND FLAGS LABEL KEY) of the menu-item fields ITEM."
  (list (plist-get item :kind) (plist-get item :flags)
        (remoteterm-tests--menu-bytes-text (plist-get item :label))
        (remoteterm-tests--menu-bytes-text (plist-get item :key))))

(defun remoteterm-tests--menu-bytes-text (bytes)
  "Return the text of the UTF-8 BYTES, a vector."
  (decode-coding-string (apply #'unibyte-string (append bytes nil))
                        'utf-8-unix))

(ert-deftest remoteterm-tests-cut-text ()
  "The model of a cut menu text gives the longest start that fits.
A trial cuts a random text at a random limit.  Law: the cut text is a
start of the text, it has at most LIMIT bytes of UTF-8, and it is the
full text, or the next character of the text makes it longer than
LIMIT.  The law does not use the model, so law M3 checks the C code
against a model that this law checks."
  (rd-gen-for-all
   (lambda ()
     (let ((text (rd-gen-text 12)))
       (list text (rd-gen-random (+ 5 (remoteterm-tests--utf-8-bytes text))))))
   (lambda (case)
     (pcase-let* ((`(,text ,limit) case)
                  (cut (remoteterm-tests--cut-text text limit)))
       (should (string-prefix-p cut text))
       (should (<= (remoteterm-tests--utf-8-bytes cut) limit))
       (should (or (equal cut text)
                   (> (remoteterm-tests--utf-8-bytes
                       (substring text 0 (1+ (length cut))))
                      limit)))))))

(defun remoteterm-tests--utf-8-bytes (text)
  "Return the number of bytes of TEXT in UTF-8."
  (length (encode-coding-string text 'utf-8-unix)))

(defun remoteterm-tests--cut-text (text limit)
  "Return the longest start of TEXT with at most LIMIT bytes of UTF-8."
  (let ((bytes 0)
        (end 0))
    (while (and (< end (length text))
                (<= (+ bytes (remoteterm-tests--utf-8-size (aref text end)))
                    limit))
      (setq bytes (+ bytes (remoteterm-tests--utf-8-size (aref text end))))
      (setq end (1+ end)))
    (substring text 0 end)))

(defun remoteterm-tests--utf-8-size (character)
  "Return the number of bytes of CHARACTER in UTF-8."
  (length (encode-coding-string (string character) 'utf-8-unix)))

(defun remoteterm-tests--well-nested-p (kinds)
  "Return non-nil if the submenu kinds in KINDS nest correctly.
Kind 2 starts a submenu, and kind 3 ends it."
  (let ((depth 0))
    (and (cl-every (lambda (kind)
                     (pcase kind
                       (2 (setq depth (1+ depth)))
                       (3 (>= (setq depth (1- depth)) 0))
                       (_ t)))
                   kinds)
         (zerop depth))))

(defun remoteterm-tests--menu-title (menu)
  "Return the title of MENU, a value of `rd-gen-menu'.
The title of a keymap menu is its prompt, and the title of a list of
panes is its first element.  No title is the empty string."
  (or (if (keymapp menu) (keymap-prompt menu) (car menu)) ""))

(defun remoteterm-tests--menu-entries (menu)
  "Return the entries of MENU, a value of `rd-gen-menu', in order.
Each entry is (KIND FLAGS LABEL KEY), as in a menu-item message, with
the label and the key text cut to their limits.  This is the model of
law M3."
  (mapcar #'car (remoteterm-tests--menu-model menu)))

(defun remoteterm-tests--menu-model (menu)
  "Return the entries of MENU, a value of `rd-gen-menu', with selections.
Each element is (ENTRY . SELECTION).  ENTRY is as for
`remoteterm-tests--menu-entries'.  SELECTION is the value of
`x-popup-menu' when the user chooses the entry: the list of the keys
to the item in a keymap menu, or the value of the item in a list of
panes.  It is nil for an entry that is not an item.  This is the model
of laws M3 and M4."
  (if (keymapp menu)
      (remoteterm-tests--keymap-entries menu nil)
    (remoteterm-tests--pane-entries (cdr menu))))

(defun remoteterm-tests--menu-entry (kind flags label key)
  "Return the entry (KIND FLAGS LABEL KEY), with LABEL and KEY cut.
LABEL and KEY are strings or nil."
  (list kind flags
        (remoteterm-tests--cut-text (or label "")
                                    remote-display--max-menu-text)
        (remoteterm-tests--cut-text (or key "") remote-display--max-menu-key)))

(defconst remoteterm-tests--menu-separator '((1 0 "" ""))
  "The model element of a separator.")

(defconst remoteterm-tests--menu-end '((3 0 "" ""))
  "The model element of the end of a submenu.")

(defun remoteterm-tests--keymap-entries (keymap path)
  "Return the model elements of the bindings of KEYMAP, in order.
PATH is the list of the keys to KEYMAP."
  (seq-mapcat (lambda (binding)
                (remoteterm-tests--binding-entries
                 (cdr binding) (append path (list (car binding)))))
              (seq-filter #'consp (cdr keymap))))

(defun remoteterm-tests--binding-entries (binding path)
  "Return the model elements of BINDING, (menu-item LABEL DEF . PROPERTIES).
PATH is the list of the keys to BINDING.  An enabled submenu gives its
start, its entries, and its end.  A disabled submenu is a disabled
item."
  (pcase-let* ((`(menu-item ,label ,def . ,properties) binding)
               (enabled (or (not (plist-member properties :enable))
                            (plist-get properties :enable)))
               (keys (plist-get properties :keys)))
    (cond ((member label rd-gen-menu-separators)
           (list remoteterm-tests--menu-separator))
          ((and (keymapp def) enabled)
           `((,(remoteterm-tests--menu-entry 2 1 label nil))
             ,@(remoteterm-tests--keymap-entries def path)
             ,remoteterm-tests--menu-end))
          (t (list (cons (remoteterm-tests--menu-entry
                          0 (remoteterm-tests--menu-flags
                             enabled (plist-get properties :button))
                          label
                          (and keys (concat "  " (substitute-command-keys
                                                  keys))))
                         path))))))

(defun remoteterm-tests--menu-flags (enabled button)
  "Return the flags of an item with ENABLED and the `:button' BUTTON."
  (logior (if enabled 1 0)
          (pcase (car button) (:toggle 2) (:radio 4) (_ 0))
          (if (cdr button) 8 0)))

(defun remoteterm-tests--pane-entries (panes)
  "Return the model elements of PANES, each (NAME ITEM...), in order.
If there are several panes, a pane with a name is a submenu with that
label.  The items of one pane, or of a pane with no name, are at the
top of the menu."
  (if (cdr panes)
      (seq-mapcat (lambda (pane)
                    (let ((items (seq-mapcat
                                  #'remoteterm-tests--pane-item-entries
                                  (cdr pane))))
                      (if (equal (car pane) "")
                          items
                        `((,(remoteterm-tests--menu-entry 2 1 (car pane) nil))
                          ,@items ,remoteterm-tests--menu-end))))
                  panes)
    (seq-mapcat #'remoteterm-tests--pane-item-entries (cdar panes))))

(defun remoteterm-tests--pane-item-entries (item)
  "Return the model elements of ITEM of a pane.
A string that is not a separator is a disabled item, and nil gives no
entry.  The selection of (LABEL . VALUE) is VALUE."
  (cond ((null item) nil)
        ((member item rd-gen-menu-separators)
         (list remoteterm-tests--menu-separator))
        ((stringp item)
         (list (list (remoteterm-tests--menu-entry 0 0 item nil))))
        (t (list (cons (remoteterm-tests--menu-entry 0 1 (car item) nil)
                       (cdr item))))))

;;;; The choice of a popup menu

(ert-deftest remoteterm-tests-menu-choice ()
  "Law M4: `x-popup-menu' gives the selection of the item that the client chose.
A trial pops up a random menu of `rd-gen-menu', at a click position or
at a list position.  A first timer sends random menu-choice messages
that the session must ignore: a choice with another serial, or a
choice of an id that is not an enabled item.  A second timer sends the
choice of a random enabled item, or a dismissal if the menu has no
enabled item.  The value of `x-popup-menu' is the selection of the
item in `remoteterm-tests--menu-model'.  After a dismissal, it is nil
for a click position, and `x-popup-menu' signals `quit' for a list
position.  The session sends one hide-menu message with the serial of
the menu, after the show-menu message."
  (skip-unless (featurep 'remote-test))
  (let ((rd-gen-trials 40))
    (rd-gen-for-all
     (lambda () (list (rd-gen-menu) (rd-gen-random 1000)
                      (rd-gen-element '(t nil))
                      (rd-gen-list #'remoteterm-tests--wrong-menu-choice 4)))
     (lambda (case) (apply #'remoteterm-tests--check-menu-choice case)))))

(ert-deftest remoteterm-tests-menu-client-change ()
  "A change of the client ends the wait of a popup menu as a dismissal.
The change is a detach, or an attach of a new client.  The new client
gets no hide-menu message, because it did not get the menu."
  (skip-unless (featurep 'remote-test))
  (dolist (change '(remote-display--detach remote-display--record))
    (dolist (click '(t nil))
      (let ((result (remoteterm-tests--answer-popup
                     (remoteterm-tests--menu-popup
                      remoteterm-tests--small-menu click)
                     (list (lambda (_shown) (funcall change "remote-menu"))))))
        (should (equal (plist-get result :value)
                       (remoteterm-tests--dismissal-value click)))
        (should-not (assq 'hide-menu (plist-get result :messages)))))))

(ert-deftest remoteterm-tests-menu-no-client ()
  "With no client, `x-popup-menu' returns at once as after a dismissal."
  (skip-unless (featurep 'remote-test))
  (dolist (click '(t nil))
    (remoteterm-tests--with-display "remote-menu"
      (remoteterm-tests--with-frame frame "remote-menu" nil
        (let ((guard (remoteterm-tests--menu-guard)))
          (should (equal (unwind-protect
                             (remoteterm-tests--catch-menu-exit
                              (funcall (remoteterm-tests--menu-popup
                                        remoteterm-tests--small-menu click)
                                       frame))
                           (cancel-timer guard))
                         (remoteterm-tests--dismissal-value click))))))))

(ert-deftest remoteterm-tests-menu-keys-wait ()
  "Key messages that come while a popup menu waits stay in the input.
The events of the keys come after `x-popup-menu' returns, in order."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--menu-keys-case)
                   '(nil (?a ?b ?c))))))

(defun remoteterm-tests--menu-keys-case ()
  "Send keys while a popup menu waits, and return the value and the events.
The value is the list (VALUE EVENTS).  VALUE is the value of
`x-popup-menu', after a dismissal for a click position.  EVENTS are
the events of the keys after `x-popup-menu' returns."
  (let ((result
         (remoteterm-tests--answer-popup
          (remoteterm-tests--menu-popup remoteterm-tests--small-menu t)
          (list (lambda (shown)
                  (dolist (key '(?a ?b ?c))
                    (remoteterm-tests--send-key shown key))
                  (remoteterm-tests--dismiss-menu shown)))
          (lambda () (remoteterm-tests--read-events 3)))))
    (list (plist-get result :value) (plist-get result :after))))

(ert-deftest remoteterm-tests-menu-timers ()
  "Timers run while a popup menu waits.
A timer that the wait starts runs three times, and then dismisses the
menu."
  (skip-unless (featurep 'remote-test))
  (let* ((count 0)
         (ticker nil)
         (result
          (unwind-protect
              (remoteterm-tests--answer-popup
               (remoteterm-tests--menu-popup remoteterm-tests--small-menu t)
               (list (lambda (shown)
                       (setq ticker
                             (run-at-time
                              0.01 0.01
                              (lambda ()
                                (when (= (cl-incf count) 3)
                                  (remoteterm-tests--dismiss-menu
                                   shown))))))))
            (when ticker
              (cancel-timer ticker)))))
    (should (equal (plist-get result :value) nil))
    (should (>= count 3))))

(ert-deftest remoteterm-tests-menu-hide-on-exit ()
  "A quit or a throw from a timer during the wait sends one hide-menu message.
The hide-menu message has the serial of the menu, and comes after the
show-menu message.  A timer catches an error, so a throw is the
non-local exit of an error in a process filter."
  (skip-unless (featurep 'remote-test))
  (dolist (exit `((quit . ,(lambda (_shown) (signal 'quit nil)))
                  (thrown . ,(lambda (_shown)
                               (throw 'remoteterm-tests--menu-exit
                                      'thrown)))))
    (let ((result (remoteterm-tests--answer-popup
                   (remoteterm-tests--menu-popup remoteterm-tests--small-menu t)
                   (list (cdr exit)))))
      (should (eq (plist-get result :value) (car exit)))
      (remoteterm-tests--check-hide-menu (plist-get result :messages)))))

(ert-deftest remoteterm-tests-menu-nested ()
  "A popup menu from a timer during the wait of a menu signals an error.
This is the check of `init_menu_items' in menu.c.  The first menu
then returns the selection of the next valid choice."
  (skip-unless (featurep 'remote-test))
  (let* ((nested nil)
         (result
          (remoteterm-tests--answer-popup
           (remoteterm-tests--menu-popup remoteterm-tests--small-menu t)
           (list (lambda (shown)
                   (setq nested
                         (condition-case err
                             (x-popup-menu (list '(0 0) (plist-get shown :frame))
                                           remoteterm-tests--small-menu)
                           (error err)))
                   (remoteterm-tests--choose-label shown "A"))))))
    (should (equal nested
                   '(error "Trying to use a menu from within a menu-entry")))
    (should (equal (plist-get result :value) '(a)))))

(ert-deftest remoteterm-tests-menu-dialog ()
  "`x-popup-dialog' on a remote frame gives the value of the chosen button."
  (skip-unless (featurep 'remote-test))
  (let ((result (remoteterm-tests--answer-popup
                 (lambda (frame)
                   (x-popup-dialog frame '("Question" ("Yes" . yes)
                                           ("No" . no))))
                 (list (lambda (shown)
                         (remoteterm-tests--choose-label shown "No"))))))
    (should (eq (plist-get result :value) 'no))))

(defconst remoteterm-tests--small-menu '(keymap (a menu-item "A" ignore))
  "A popup menu with one enabled item.")

(defun remoteterm-tests--check-menu-choice (menu pick click wrongs)
  "Check law M4 for MENU, PICK, CLICK, and WRONGS.
PICK selects the enabled item to choose, CLICK is non-nil for a click
position, and WRONGS are values of `remoteterm-tests--wrong-menu-choice'."
  (let* ((model (remoteterm-tests--menu-model menu))
         (enabled (seq-filter
                   (lambda (index)
                     (remoteterm-tests--enabled-item-p (car (nth index model))))
                   (number-sequence 0 (1- (length model)))))
         (index (and enabled (nth (mod pick (length enabled)) enabled)))
         (result
          (remoteterm-tests--answer-popup
           (remoteterm-tests--menu-popup menu click)
           (list (lambda (shown)
                   (dolist (wrong wrongs)
                     (apply #'remoteterm-tests--send-menu-choice
                            (remoteterm-tests--wrong-choice shown wrong))))
                 (lambda (shown)
                   (if index
                       (remoteterm-tests--choose-index shown index)
                     (remoteterm-tests--dismiss-menu shown)))))))
    (should (equal (plist-get result :value)
                   (if index
                       (cdr (nth index model))
                     (remoteterm-tests--dismissal-value click))))
    (remoteterm-tests--check-hide-menu (plist-get result :messages))))

(defun remoteterm-tests--enabled-item-p (entry)
  "Return non-nil if the model ENTRY is an enabled item."
  (and (eql (nth 0 entry) 0) (eql (logand (nth 1 entry) 1) 1)))

(defun remoteterm-tests--wrong-menu-choice ()
  "Return a random menu-choice that a session must ignore, as (KIND N).
KIND `serial' is a choice with another serial, and KIND `id' is the
choice of an id that is not an enabled item.  N selects the serial or
the id (see `remoteterm-tests--wrong-choice')."
  (list (rd-gen-element '(serial id)) (rd-gen-random 1000)))

(defun remoteterm-tests--wrong-choice (shown wrong)
  "Return the arguments (SERIAL CHOSEN ID) of the menu-choice WRONG.
SHOWN is the menu that the client got (see
`remoteterm-tests--answer-popup'), and WRONG is a value of
`remoteterm-tests--wrong-menu-choice'."
  (pcase-let* ((`(,kind ,n) wrong)
               (serial (plist-get shown :serial))
               (items (plist-get shown :items))
               (ids (mapcar (lambda (item) (plist-get item :id)) items)))
    (if (eq kind 'serial)
        (list (+ serial 1 n) (logand n 1) (or (car (last ids)) 0))
      (let ((wrong-ids (cons (+ (apply #'max 0 ids) 1 n)
                             (mapcar (lambda (item) (plist-get item :id))
                                     (seq-remove
                                      #'remoteterm-tests--enabled-message-p
                                      items)))))
        (list serial 1 (nth (mod n (length wrong-ids)) wrong-ids))))))

(defun remoteterm-tests--enabled-message-p (item)
  "Return non-nil if the menu-item fields ITEM are an enabled item."
  (and (eql (plist-get item :kind) 0)
       (eql (logand (plist-get item :flags) 1) 1)))

(defun remoteterm-tests--check-hide-menu (messages)
  "Check that MESSAGES have one hide-menu message for the shown menu.
The hide-menu message comes after the show-menu message, and has its
serial."
  (let ((menu-messages (seq-filter (lambda (message)
                                     (memq (car message)
                                           '(show-menu hide-menu)))
                                   messages)))
    (should (equal (mapcar #'car menu-messages) '(show-menu hide-menu)))
    (should (eql (plist-get (cdr (nth 0 menu-messages)) :serial)
                 (plist-get (cdr (nth 1 menu-messages)) :serial)))))

(defun remoteterm-tests--dismissal-value (click)
  "Return the value of a popup menu after a dismissal.
CLICK is non-nil for a click position.  The value `quit' means that
`x-popup-menu' signals `quit'."
  (if click nil 'quit))

(defun remoteterm-tests--menu-popup (menu click)
  "Return a function of a frame that pops up MENU on the frame.
CLICK non-nil means that the position is a click event, and nil means
the list ((X Y) FRAME)."
  (lambda (frame)
    (x-popup-menu (if click
                      (list 'mouse-3 (list (frame-root-window frame) 1
                                           '(10 . 20) 0))
                    (list '(10 20) frame))
                  menu)))

(defun remoteterm-tests--answer-popup (popup answers &optional after)
  "Call POPUP with a remote frame, and answer the menu with ANSWERS.
POPUP pops up a menu on its frame.  The functions of ANSWERS run in
timers, in order, while POPUP waits: the first one when the client got
the menu, and each other one 50 milliseconds after the one before it.
The argument of each function is the menu that the client got: a
plist with :serial, the serial of the show-menu message, :items, the
fields of the menu-item messages in order, and :frame, the frame.
AFTER, if
non-nil, is a function that runs after POPUP returns.  The value is a
plist.  :value is the value of POPUP, or the cause of its exit (see
`remoteterm-tests--catch-menu-exit').  :messages are the messages that
the session sent to its clients, in order.  :after is the value of
AFTER."
  (remoteterm-tests--with-display "remote-menu"
    (remoteterm-tests--with-frame frame "remote-menu" nil
      (remote-display--record "remote-menu")
      (let* ((output "")
             (take (lambda ()
                     (setq output (concat output
                                          (remoteterm-tests--take-menu-output)))))
             (stop (remoteterm-tests--run-menu-answers
                    answers
                    (lambda ()
                      (funcall take)
                      (remoteterm-tests--shown-menu output frame))))
             (guard (remoteterm-tests--menu-guard))
             (value (unwind-protect
                        (remoteterm-tests--catch-menu-exit
                         (funcall popup frame))
                      (cancel-timer guard)
                      (funcall stop))))
        (funcall take)
        (list :value value :messages (remoteterm-tests--all-messages output)
              :after (and after (funcall after)))))))

(defun remoteterm-tests--run-menu-answers (answers shown)
  "Call the functions of ANSWERS in timers, after the client got a menu.
SHOWN returns the menu that the client got, or nil if the client did
not get a menu yet.  Call the first function when SHOWN returns a
menu, and each other function 50 milliseconds after the function
before it.  Give the menu to each function.  Return a function that
stops the timers.  A timer is necessary, because `x-popup-dialog' can
run timers before it shows the menu."
  (let ((timer nil)
        (step nil))
    (setq step (lambda (answers)
                 (let ((menu (funcall shown)))
                   (cond ((null answers))
                         ((null menu)
                          (setq timer (run-at-time 0.01 nil step answers)))
                         (t (funcall (car answers) menu)
                            (setq timer (run-at-time 0.05 nil step
                                                     (cdr answers))))))))
    (setq timer (run-at-time 0 nil step answers))
    (lambda () (cancel-timer timer))))

(defun remoteterm-tests--take-menu-output ()
  "Return the output of the session \"remote-menu\".
The value is the empty string if the session has no recording client."
  (or (ignore-errors (remote-display--take-output "remote-menu")) ""))

(defun remoteterm-tests--shown-menu (output frame)
  "Return the menu of the bytes OUTPUT for FRAME, or nil if it has none.
See `remoteterm-tests--answer-popup'."
  (let* ((messages (remoteterm-tests--all-messages output))
         (show (alist-get 'show-menu messages)))
    (and show
         (list :serial (plist-get show :serial)
               :items (mapcar #'cdr (seq-filter (lambda (message)
                                                  (eq (car message)
                                                      'menu-item))
                                                messages))
               :frame frame))))

(defun remoteterm-tests--send-menu-choice (serial chosen id)
  "Send a menu-choice message with SERIAL, CHOSEN, and ID to the session."
  (remote-display--input "remote-menu"
                         (remote-display--encode
                          `(menu-choice :serial ,serial :chosen ,chosen
                                        :id ,id))))

(defun remoteterm-tests--dismiss-menu (shown)
  "Send the dismissal of the menu SHOWN."
  (remoteterm-tests--send-menu-choice (plist-get shown :serial) 0 0))

(defun remoteterm-tests--choose-index (shown index)
  "Send the choice of the entry at INDEX of the menu SHOWN."
  (remoteterm-tests--send-menu-choice
   (plist-get shown :serial) 1
   (plist-get (nth index (plist-get shown :items)) :id)))

(defun remoteterm-tests--choose-label (shown label)
  "Send the choice of the item with LABEL of the menu SHOWN."
  (remoteterm-tests--choose-index
   shown (seq-position (plist-get shown :items) label
                       (lambda (item label)
                         (equal (remoteterm-tests--menu-bytes-text
                                 (plist-get item :label))
                                label)))))

(defun remoteterm-tests--send-key (shown character)
  "Send a key message for CHARACTER on the frame of the menu SHOWN."
  (remote-display--input
   "remote-menu"
   (remote-display--encode
    `(key :frame ,(remoteterm-tests--frame-id (plist-get shown :frame))
          :time 0 :keysym ,character :character ,character :modifiers 0))))

;;;; The pace of a large popup menu

(ert-deftest remoteterm-tests-menu-paced ()
  "Law M5: the entries of a popup menu go at the pace of the connection.
A trial pops up a large random menu of `rd-gen-large-menu' while the
sink of the session reports 65536 bytes.  Then a timer drains the
queue, or quits, throws, or detaches the client while the entries
wait.  See `remoteterm-tests--paced-popup'."
  (skip-unless (featurep 'remote-test))
  (let ((rd-gen-trials 8))
    (rd-gen-for-all
     (lambda () (list (rd-gen-large-menu)
                      (rd-gen-element '(drain quit throw detach))))
     (lambda (case)
       (apply #'remoteterm-tests--check-menu-paced
              (car case) (cadr case)
              (list (apply #'remoteterm-tests--paced-popup case)))))))

(defun remoteterm-tests--check-menu-paced (menu exit result)
  "Check law M5 for MENU, EXIT, and RESULT.
RESULT is the value of `remoteterm-tests--paced-popup' for MENU and
EXIT."
  (pcase-let (((map :full :sent :value :after) result))
    (should-not (remoteterm-tests--menu-messages full))
    (if (eq exit 'drain)
        (let ((menu-messages (remoteterm-tests--menu-messages sent)))
          (should (equal (mapcar #'car menu-messages)
                         (append (make-list (1- (length menu-messages))
                                            'menu-item)
                                 '(show-menu))))
          (should (equal (mapcar (lambda (message)
                                   (remoteterm-tests--menu-item-entry
                                    (cdr message)))
                                 (butlast menu-messages))
                         (remoteterm-tests--menu-entries menu)))
          (should (eq value nil))
          (remoteterm-tests--check-hide-menu (append sent after)))
      (should (eq value (if (eq exit 'detach) nil exit)))
      (should-not (remoteterm-tests--menu-messages sent))
      (should-not (remoteterm-tests--menu-messages after)))))

(defun remoteterm-tests--menu-messages (messages)
  "Return the menu-item, show-menu, and hide-menu messages of MESSAGES."
  (seq-filter (lambda (message)
                (memq (car message) '(menu-item show-menu hide-menu)))
              messages))

(defun remoteterm-tests--paced-popup (menu exit)
  "Pop up MENU at a click while the sink is full, and end it with EXIT.
A timer takes the output while the sink reports 65536 bytes.  Then
EXIT `drain' reports 65535 bytes and an empty queue, takes the output,
and sends a dismissal.  EXIT `quit' signals `quit', `throw' throws
`throw' to `remoteterm-tests--catch-menu-exit', and `detach' detaches
the client.  After `x-popup-menu' returns, the sink reports 65535 bytes
and an empty queue again.  The value is a plist.  :full and :sent are
the messages before and after the first empty queue.  :value is the
value of `x-popup-menu', or the cause of its exit.  :after are the
messages after `x-popup-menu' returns."
  (remoteterm-tests--with-display "remote-menu"
    (remoteterm-tests--with-frame frame "remote-menu" nil
      (remote-display--record "remote-menu")
      (remote-display--take-output "remote-menu")
      (remote-display--set-queued "remote-menu" 65536)
      (let* ((state nil)
             (step (run-at-time
                    0 nil
                    #'remoteterm-tests--paced-menu-exit
                    exit (lambda (result) (setq state result))))
             (guard (remoteterm-tests--menu-guard))
             (value (unwind-protect
                        (remoteterm-tests--catch-menu-exit
                         (funcall (remoteterm-tests--menu-popup menu t) frame))
                      (cancel-timer step)
                      (cancel-timer guard))))
        (append (list :value value
                      :after (remoteterm-tests--drain-menu-output))
                state)))))

(defun remoteterm-tests--paced-menu-exit (exit save)
  "Take the output of the full sink, and then do EXIT.
See `remoteterm-tests--paced-popup'.  Call SAVE with the plist of :full
and :sent before EXIT, because a quit or a throw does not return."
  (let ((full (remoteterm-tests--all-messages
               (remoteterm-tests--take-menu-output))))
    (if (not (eq exit 'drain))
        (funcall save (list :full full :sent nil))
      (let* ((sent (remoteterm-tests--drain-menu-output))
             (show (alist-get 'show-menu sent)))
        (funcall save (list :full full :sent sent))
        (remoteterm-tests--send-menu-choice (or (plist-get show :serial) 0)
                                            0 0)))
    (pcase exit
      ('quit (signal 'quit nil))
      ('throw (throw 'remoteterm-tests--menu-exit 'throw))
      ('detach (remote-display--detach "remote-menu")))))

(defun remoteterm-tests--drain-menu-output ()
  "Report 65535 bytes and an empty queue, and return the messages after.
The session is \"remote-menu\".  A session with no client gives no
messages."
  (ignore-errors (remote-display--set-queued "remote-menu" 65535))
  (remote-display--queue-empty "remote-menu")
  (remoteterm-tests--all-messages (remoteterm-tests--take-menu-output)))

;;;; Font transfer

(ert-deftest remoteterm-tests-font-transfer ()
  "The session sends the font file of a want-font message (law 40)."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--font-transfer-case)
                   '(:hash t :size t :offsets t :unknown nil
                     :redefined t)))))

(defun remoteterm-tests--font-transfer-case ()
  "Ask a session for the file of a font of its frame, and for an unknown file.
Return a plist.  :hash and :size are t if the bytes of the font-data
messages have the hash and the file size of the define-font message.
:offsets is t if the offset of each font-data message is the number of
bytes before it, and its total is the file size.  :unknown is t if a
font-data message has the unknown hash.  :redefined is t if a
define-font message for the font and a draw-glyphs message with it
come after the last font-data message."
  (remoteterm-tests--with-display "remote-font"
    (let* ((socket (remote-display-listen "remote-font"))
           (frame (remoteterm-tests--make-frame "remote-font" nil))
           (buffer (generate-new-buffer "font"))
           (connection (remoteterm-tests--connect socket))
           (unknown (make-vector 32 #xee)))
      (set-window-buffer (frame-root-window frame) buffer)
      (select-frame frame)
      (with-current-buffer buffer
        (insert "abc"))
      (unwind-protect
          (let* ((define (remoteterm-tests--await-define-font connection))
                 (fields (cdr define))
                 (hash (plist-get fields :hash))
                 (size (plist-get fields :file-size)))
            (setcar (cdr connection) nil)
            (process-send-string
             (car connection)
             (concat (remote-display--encode `(want-font :hash ,unknown))
                     (remote-display--encode
                      `(want-font :hash ,(seq-take hash 31)))
                     (remote-display--encode `(want-font :hash ,hash))
                     (remote-display--encode `(want-font :hash ,hash))))
            (let* ((messages (remoteterm-tests--await-font-data
                              connection hash size))
                   (data (seq-filter (lambda (message)
                                       (eq (car message) 'font-data))
                                     messages))
                   (after (cdr (memq (car (last data)) messages)))
                   (bytes (apply #'unibyte-string
                                 (seq-mapcat
                                  (lambda (message)
                                    (append (plist-get (cdr message) :bytes)
                                            nil))
                                  data))))
              (list :hash (equal (secure-hash 'sha256 bytes nil nil t)
                                 (apply #'unibyte-string (append hash nil)))
                    :size (= (length bytes) size)
                    :offsets (remoteterm-tests--font-offsets-p data size)
                    :unknown (and (seq-find
                                   (lambda (message)
                                     (equal (plist-get (cdr message) :hash)
                                            unknown))
                                   data)
                                  t)
                    :redefined (remoteterm-tests--redefined-p
                                after (plist-get fields :font)))))
        (delete-process (car connection))
        (delete-frame frame)
        (kill-buffer buffer)))))

(ert-deftest remoteterm-tests-font-pace ()
  "Font data waits while the sink of the session is full (law 41)."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let (((map :full :rest :size :dropped)
                 (remoteterm-tests--daemon-call
                  'remoteterm-tests--font-pace-case)))
      (should (= full 0))
      (should (= rest size))
      (should (= dropped 0)))))

(defun remoteterm-tests--font-pace-case ()
  "Ask for a font file while the sink of the session is full.
The value of the form of `remoteterm-tests--font-pace-form'."
  (eval (remoteterm-tests--font-pace-form) t))

(defun remoteterm-tests--font-pace-form ()
  "Return a form that asks for a font file with a full sink.
The value of the form is a plist.  :full is the number of bytes of
font data that the session sends while the sink reports 65536 bytes.
:rest is the number of bytes of font data after the sink reports
65535 bytes and an empty queue.  :size is the file size of the font.
:dropped is the number of bytes of font data that a new client gets
after the old client asked for the file with a full sink."
  (remoteterm-tests--frame-form
   '((width . 30))
   '(insert "abc")
   '(let* ((define (seq-find (lambda (message)
                               (and (eq (car message) 'define-font)
                                    (= (length (plist-get (cdr message)
                                                          :hash))
                                       32)))
                             (remoteterm-tests--all-messages output)))
           (hash (plist-get (cdr define) :hash))
           (want (remote-display--encode `(want-font :hash ,hash)))
           (data-bytes (lambda ()
                         (remoteterm-tests--font-data-bytes
                          (remote-display--take-output display)))))
      (remote-display--set-queued display 65536)
      (remote-display--input display want)
      (list :full (funcall data-bytes)
            :rest (progn (remote-display--set-queued display 65535)
                         (remote-display--queue-empty display)
                         (funcall data-bytes))
            :size (plist-get (cdr define) :file-size)
            :dropped (progn (remote-display--set-queued display 65536)
                            (remote-display--input display want)
                            (remote-display--record display)
                            (remote-display--queue-empty display)
                            (funcall data-bytes))))))

(defun remoteterm-tests--font-data-bytes (bytes)
  "Return the number of bytes of font data in the messages of BYTES."
  (apply #'+ (mapcar (lambda (message)
                       (if (eq (car message) 'font-data)
                           (length (plist-get (cdr message) :bytes))
                         0))
                     (remoteterm-tests--all-messages bytes))))

(ert-deftest remoteterm-tests-font-changed ()
  "A font file that changed after its definition is not sent (law 40)."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--font-changed-case)
                   '(:defined t :data 0)))))

(defun remoteterm-tests--font-changed-case ()
  "Define a font from a copy of its file, change the copy, and ask for it.
Return a plist.  :defined is t if the session defined the font with the
hash of the copy.  :data is the number of bytes of font data after the
want-font message."
  (let ((copy (make-temp-file "rd-font")))
    (unwind-protect
        (eval (remoteterm-tests--font-changed-form copy) t)
      (delete-file copy))))

(defun remoteterm-tests--font-changed-form (copy)
  "Return a form that draws with a font whose file is COPY, and changes COPY.
COPY gets the bytes of the default font file of the frame first, and
one more byte, so no other font file has its hash.  The value of the
form is as for `remoteterm-tests--font-changed-case'."
  (remoteterm-tests--frame-form
   '((width . 30))
   `(progn
      (copy-file (aref (font-info (face-font 'default frame) frame) 12)
                 ,copy t)
      (let ((coding-system-for-write 'no-conversion))
        (write-region "\0" nil ,copy t 'silent))
      (remoteterm-tests--insert-font-lines
       (list ,copy) frame (cl-incf remoteterm-tests--last-font-size) nil))
   `(let* ((hash (remote-display--file-hash ,copy))
           (define (seq-find (lambda (message)
                               (and (eq (car message) 'define-font)
                                    (equal (plist-get (cdr message) :hash)
                                           hash)))
                             (remoteterm-tests--all-messages output))))
      (let ((coding-system-for-write 'no-conversion))
        (write-region "x" nil ,copy nil 'silent))
      (remote-display--input display
                             (remote-display--encode
                              `(want-font :hash ,hash)))
      (list :defined (and define t)
            :data (remoteterm-tests--font-data-bytes
                   (remote-display--take-output display))))))

(defun remoteterm-tests--received-messages (connection)
  "Return the messages that CONNECTION got.
CONNECTION is (PROCESS . RECEIVED), as `remoteterm-tests--connect'
returns.  A message that is not complete is left out."
  (let ((bytes (string-to-unibyte
                (apply #'concat (reverse (cadr connection)))))
        (messages nil)
        (start 0))
    (while (let ((length (and (<= (+ start 4) (length bytes))
                              (+ 4 (logior (ash (aref bytes start) 24)
                                           (ash (aref bytes (+ start 1)) 16)
                                           (ash (aref bytes (+ start 2)) 8)
                                           (aref bytes (+ start 3)))))))
             (and length (<= (+ start length) (length bytes))))
      (let ((result (remote-display--decode bytes start)))
        (push (car result) messages)
        (setq start (+ start (cdr result)))))
    (nreverse messages)))

(defun remoteterm-tests--await-define-font (connection)
  "Attach CONNECTION, and return the first define-font message with a hash.
Wait at most 10 seconds, with a redisplay at each step."
  (remoteterm-tests--reset-and-ping connection 7)
  (let ((end (+ (float-time) 10))
        (found nil))
    (while (and (not found) (< (float-time) end))
      (redisplay t)
      (accept-process-output (car connection) 0.05)
      (setq found (seq-find
                   (lambda (message)
                     (and (eq (car message) 'define-font)
                          (= (length (plist-get (cdr message) :hash)) 32)))
                   (remoteterm-tests--received-messages connection))))
    found))

(defun remoteterm-tests--await-font-data (connection hash size)
  "Return the messages of CONNECTION after the font-data of HASH and SIZE.
Wait until the font-data messages for HASH have SIZE bytes, and then
for a draw-glyphs message after them, with a redisplay at each step.
Wait at most 10 seconds."
  (let ((end (+ (float-time) 10))
        (messages nil))
    (while (and (< (float-time) end)
                (not (remoteterm-tests--complete-p messages hash size)))
      (redisplay t)
      (accept-process-output (car connection) 0.05)
      (setq messages (remoteterm-tests--received-messages connection)))
    messages))

(defun remoteterm-tests--complete-p (messages hash size)
  "Return non-nil if MESSAGES have SIZE bytes of HASH and then a draw."
  (let ((got 0)
        (drawn nil))
    (dolist (message messages)
      (pcase (car message)
        ('font-data
         (when (equal (plist-get (cdr message) :hash) hash)
           (setq got (+ got (length (plist-get (cdr message) :bytes))))
           (setq drawn nil)))
        ('draw-glyphs (setq drawn (>= got size)))))
    (and (>= got size) drawn)))

(defun remoteterm-tests--font-offsets-p (data size)
  "Return t if each message of DATA starts after the bytes before it.
Each message must also have the total SIZE."
  (let ((offset 0)
        (correct t))
    (dolist (message data)
      (let ((fields (cdr message)))
        (unless (and (= (plist-get fields :offset) offset)
                     (= (plist-get fields :total) size))
          (setq correct nil))
        (setq offset (+ offset (length (plist-get fields :bytes))))))
    correct))

(defun remoteterm-tests--redefined-p (messages font)
  "Return t if MESSAGES define FONT, and then draw glyphs with it."
  (let ((defined (seq-position messages font
                               (lambda (message id)
                                 (and (eq (car message) 'define-font)
                                      (equal (plist-get (cdr message) :font)
                                             id))))))
    (and defined
         (seq-find (lambda (message)
                     (and (eq (car message) 'draw-glyphs)
                          (equal (plist-get (cdr message) :font) font)))
                   (nthcdr defined messages))
         t)))

(ert-deftest remoteterm-tests-listen-end-closes-client ()
  "When a session ends, the connection of its client closes.
Thus a client knows that the session is gone, and can stop.  The
socket is gone before the close, so a relay that sees the close can
see that the session ended."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--listen-end-case)
                   '(closed nil)))))

(defun remoteterm-tests--listen-end-case ()
  "Attach a client to a session, delete the last frame, and return a list.
The list is (STATUS SOCKET): STATUS is the status of the client side
of the connection after the session ends, or after 10 seconds.
SOCKET is non-nil if the socket existed when the session deleted the
connection of its client."
  (remoteterm-tests--with-display "remote-listen-a"
    (let* ((socket (remote-display-listen "remote-listen-a"))
           (frame (remoteterm-tests--make-frame "remote-listen-a" nil))
           (connection (remoteterm-tests--connect socket))
           (socket-at-close 'not-closed))
      (unwind-protect
          (progn
            (remoteterm-tests--reset-and-ping connection 7)
            (remoteterm-tests--pong-of (car connection) (cdr connection))
            (let ((client (remote-display-client "remote-listen-a")))
              (cl-letf* ((delete (symbol-function 'delete-process))
                         ((symbol-function 'delete-process)
                          (lambda (&optional process)
                            (when (eq process client)
                              (setq socket-at-close (file-exists-p socket)))
                            (funcall delete process))))
                (delete-frame frame)))
            (with-timeout (10)
              (while (eq (process-status (car connection)) 'open)
                (accept-process-output nil 0.05)))
            (list (process-status (car connection)) socket-at-close))
        (delete-process (car connection))))))

(ert-deftest remoteterm-tests-listen-kill-emacs ()
  "When Emacs exits, the socket of each session and its directory go away."
  (skip-unless (featurep 'remote-test))
  (let ((socket
         (with-temp-buffer
           (call-process
            (expand-file-name invocation-name invocation-directory)
            nil '(t nil) nil "-Q" "--batch" "--eval"
            "(progn (x-open-connection \"remote-kill\")
                    (princ (remote-display-listen \"remote-kill\"))
                    (kill-emacs 0))")
           (buffer-string))))
    (should (string-suffix-p "/socket" socket))
    (should-not (file-exists-p (file-name-directory socket)))))

(ert-deftest remoteterm-tests-listen-two-sessions ()
  "Two sessions have sockets in different directories.
With no XDG_RUNTIME_DIR, the directory is in `temporary-file-directory'."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (remoteterm-tests--daemon-call
             'remoteterm-tests--listen-two-sessions-case))))

(defun remoteterm-tests--listen-two-sessions-case ()
  "Return t if two sessions have sockets in different directories.
The second session listens with no XDG_RUNTIME_DIR, so its directory
must be in `temporary-file-directory'."
  (remoteterm-tests--with-display "remote-listen-a"
    (remoteterm-tests--with-display "remote-listen-b"
      (let ((a (file-name-directory
                (remote-display-listen "remote-listen-a")))
            (b (file-name-directory
                (let ((process-environment
                       (cons "XDG_RUNTIME_DIR" process-environment)))
                  (remote-display-listen "remote-listen-b")))))
        (and (not (equal a b))
             (file-in-directory-p b temporary-file-directory))))))

(ert-deftest remoteterm-tests-listen-start-fails ()
  "When the server cannot start, the listen signals an error.
The directory of the socket is deleted, and the session can listen
later."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--listen-start-fails-case)
                   '(t nil nil)))))

(defun remoteterm-tests--listen-start-fails-case ()
  "Listen with a socket name that is longer than the limit of the system.
Return (ERROR LEFT LATER-ERROR): ERROR is t if the listen signals an
error, LEFT is the list of the directories that are left, and
LATER-ERROR is the error of a later listen with a short name."
  (let* ((long (make-temp-file
                (expand-file-name (make-string 120 ?d)
                                  temporary-file-directory)
                t)))
    (unwind-protect
        (remoteterm-tests--with-display "remote-listen-c"
          (let ((error (let ((process-environment
                              (cons (concat "XDG_RUNTIME_DIR=" long)
                                    process-environment)))
                         (condition-case nil
                             (progn (remote-display-listen "remote-listen-c")
                                    nil)
                           (error t)))))
            (list error
                  (directory-files long nil "\\`remote-display-")
                  (condition-case err
                      (progn (remote-display-listen "remote-listen-c") nil)
                    (error err)))))
      (delete-directory long t))))

(ert-deftest remoteterm-tests-listen-no-exit-query ()
  "Emacs does not ask about the server of a socket when it exits."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--listen-query-case)
                   '(nil)))))

(defun remoteterm-tests--listen-query-case ()
  "Return the exit query flags of the servers of a listening session."
  (remoteterm-tests--with-display "remote-listen-a"
    (remote-display-listen "remote-listen-a")
    (mapcar #'process-query-on-exit-flag
            (seq-filter (lambda (process)
                          (equal (process-name process)
                                 "remote-display-listen remote-listen-a"))
                        (process-list)))))

;;;; Start

(rd-daemon-deftest remoteterm-tests-start-gives-session
  "`remote-display-start' starts a new session with a frame and a socket.
Each call gives a session with a new name.  The directory of the
socket has the mode 700.  A client of the socket gets a define-frame
message for the frame of its session.  The name of the session has 1
to 32 ASCII letters, digits, and \"-\" characters, so the option
--session of remote-client accepts it (`rc_ssh_session_name_p' in
remote-client/sshargs.c)."
  (let ((rd-gen-trials 5))
    (rd-gen-for-all
     (lambda () (rd-gen-integer 1 3))
     (lambda (count)
       (let ((results (remoteterm-tests--start-case count)))
         (should (= (length (delete-dups
                             (mapcar (lambda (result)
                                       (plist-get result :socket))
                                     results)))
                    count))
         (dolist (result results)
           (should (= (plist-get result :mode) #o700))
           (should (plist-get result :frame-defined))
           (should (string-match-p "\\`[A-Za-z0-9-]\\{1,32\\}\\'"
                                   (plist-get result :display)))))))))

(defun remoteterm-tests--start-case (count)
  "Call `remote-display-start' COUNT times, and connect to each socket.
Return a plist for each new session: :socket is the socket, :mode is
the mode of its directory, :frame-defined is non-nil if the client got
a define-frame message, and :display is the name of the session.  Then
delete the new sessions."
  (let* ((before (x-display-list))
         (connections nil)
         (sockets (cl-loop repeat count
                           collect (let* ((known (x-display-list))
                                          (socket (remote-display-start)))
                                     (cons (car (seq-difference
                                                 (x-display-list) known))
                                           socket)))))
    (unwind-protect
        (mapcar
         (lambda (socket)
           (let ((connection (remoteterm-tests--connect socket)))
             (push connection connections)
             (list :socket socket
                   :mode (file-modes (file-name-directory socket))
                   :frame-defined (remoteterm-tests--frame-defined-p
                                   connection)
                   :display (car (rassoc socket sockets)))))
         (mapcar #'cdr sockets))
      (dolist (connection connections)
        (delete-process (car connection)))
      (dolist (display (seq-difference (x-display-list) before))
        (dolist (frame (frames-on-display-list display))
          (delete-frame frame t))
        (when (remoteterm-tests--terminal display)
          (x-close-connection display))))))

(ert-deftest remoteterm-tests-resume-session ()
  "`remote-display-resume' gives the socket of an open session.
A new client of that socket gets the frame of the session again.  The
name of a session that is not open is an error."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--resume-case)
                   '(:same t :defined t
                     :unknown (error "No open session remote-not-open"))))))

(defun remoteterm-tests--resume-case ()
  "Start two sessions, end the client of the first, and resume it.
Return a plist.  :same is t if `remote-display-resume' gives the socket
of `remote-display-start' for each session.  :defined is t if a new
client of the socket of the first session gets a define-frame message.
:unknown is the error of a resume of a session that is not open."
  (let* ((before (x-display-list))
         (socket (remote-display-start))
         (display (car (seq-difference (x-display-list) before)))
         (other-socket (remote-display-start))
         (other (car (seq-difference (x-display-list)
                                     (cons display before))))
         (connections nil))
    (unwind-protect
        (let ((first (remoteterm-tests--connect socket)))
          (push first connections)
          (remoteterm-tests--frame-defined-p first)
          (delete-process (car first))
          (let* ((resumed (remote-display-resume display))
                 (second (remoteterm-tests--connect resumed)))
            (push second connections)
            (list :same (and (equal resumed socket)
                             (equal (remote-display-resume other)
                                    other-socket))
                  :defined (and (remoteterm-tests--frame-defined-p second) t)
                  :unknown (condition-case err
                               (remote-display-resume "remote-not-open")
                             (error err)))))
      (dolist (connection connections)
        (delete-process (car connection)))
      (dolist (session (list display other))
        (dolist (frame (frames-on-display-list session))
          (delete-frame frame t))
        (when (remoteterm-tests--terminal session)
          (x-close-connection session))))))

(defun remoteterm-tests--frame-defined-p (connection)
  "Return non-nil if CONNECTION gets a define-frame message in 10 seconds.
CONNECTION is (PROCESS . RECEIVED), as `remoteterm-tests--connect'
returns.  The test has the daemon, so the daemon is not idle and does
not redisplay: the loop calls `redisplay'."
  (let ((end (+ (float-time) 10)) (found nil))
    (while (and (not (setq found
                           (seq-find
                            (lambda (message)
                              (eq (car message) 'define-frame))
                            (remoteterm-tests--all-messages
                             (string-to-unibyte
                              (apply #'concat
                                     (reverse (cadr connection))))))))
                (< (float-time) end))
      (accept-process-output (car connection) 0.05)
      (redisplay t))
    found))

(defun remoteterm-tests--reset-and-ping (connection serial)
  "Forget the strings that CONNECTION got, and send a ping with SERIAL.
CONNECTION is (PROCESS . RECEIVED), as `remoteterm-tests--connect'
returns."
  (setcar (cdr connection) nil)
  (process-send-string (car connection)
                       (remote-display--encode (list 'ping :serial serial))))

(defun remoteterm-tests--pong-of (process received)
  "Return the serial of the first pong that PROCESS gets.
RECEIVED is a list whose car has the strings that PROCESS got.  Return
nil if no pong comes in 10 seconds."
  (let ((end (+ (float-time) 10)))
    (while (and (not (remoteterm-tests--read-pong received))
                (< (float-time) end))
      (accept-process-output process 0.05))
    (remoteterm-tests--read-pong received)))

(defun remoteterm-tests--read-pong (received)
  "Return the serial of the first pong in RECEIVED, or nil.
RECEIVED is a list whose car has the strings that a process got."
  (let ((pong (seq-find
               (lambda (message) (eq (car message) 'pong))
               (remoteterm-tests--all-messages
                (string-to-unibyte
                 (apply #'concat (reverse (car received))))))))
    (and pong (plist-get (cdr pong) :serial))))

(rd-daemon-deftest remoteterm-tests-ping-gives-pong
  "The server answers each ping of its client with a pong of the same serial.
The pongs come in the order of the pings, and the chunks of the bytes
do not change them."
  (rd-gen-for-all
   #'remoteterm-tests--pings
   (lambda (case)
     (should (equal (remoteterm-tests--pongs-of case) (car case))))))

(defun remoteterm-tests--pings ()
  "Return a random list of (SERIALS CHUNKS).
SERIALS is a list of the serials of pings, and CHUNKS is a list of the
sizes of the parts of their bytes."
  (list (cl-loop repeat (rd-gen-integer 1 20) collect (rd-gen-uint32))
        (cl-loop repeat (rd-gen-integer 0 10)
                 collect (rd-gen-integer 1 7))))

(defun remoteterm-tests--pongs-of (case)
  "Send the pings of CASE, a `remoteterm-tests--pings', to a session.
Return the serials of the pongs that the client gets."
  (remoteterm-tests--with-display "remote-ping"
    (remoteterm-tests--with-connections
     '(a)
     (lambda (connections)
       (let* ((connection (alist-get 'a connections))
              (chunks (plist-get connection :chunks))
              (pongs nil))
         (remote-display-attach "remote-ping"
                                (plist-get connection :server-side))
         (remoteterm-tests--send-in-chunks
          (plist-get connection :client-side)
          (mapconcat (lambda (serial)
                       (remote-display--encode (list 'ping :serial serial)))
                     (car case))
          (cadr case))
         (cl-flet ((read-pongs ()
                     (setq pongs
                           (seq-filter
                            (lambda (message) (eq (car message) 'pong))
                            (remoteterm-tests--all-messages
                             (string-to-unibyte
                              (apply #'concat
                                     (reverse (funcall chunks)))))))))
           (with-timeout (10 (error "The pongs did not come: %S" pongs))
             (while (< (length pongs) (length (car case)))
               (accept-process-output nil 0.05)
               (read-pongs)))
           ;; Read once more, so that an extra pong is in the result.
           (accept-process-output nil 0.1)
           (read-pongs))
         (mapcar (lambda (pong) (plist-get (cdr pong) :serial)) pongs))))))

;;;; Mouse input

(defconst remoteterm-tests--wheel-buttons
  '((4 . wheel-up) (5 . wheel-down) (6 . wheel-left) (7 . wheel-right))
  "The buttons of the mouse wheel, with the event of each.")

(defun remoteterm-tests--random-mouse-input ()
  "Return a random mouse input.
An input is (click B M X Y DX DY), (wheel B M X Y), or (ignored
MESSAGE...).  B is a button, and M has 8 random bits, of which
`remote-display--modifier-bits' names some.  X and Y are fractions
of the text area of the window: the daemon turns them into pixels.
A click is a press at X, Y and a release DX, DY pixels from it.  An
ignored input is messages that give no event: a press and a release of
the button 0, a release of a wheel button, or a press of a frame that
does not exist."
  (let ((modifiers (rd-gen-integer 0 255))
        (x (/ (rd-gen-integer 10 90) 100.0))
        (y (/ (rd-gen-integer 10 90) 100.0)))
    (pcase (rd-gen-integer 0 5)
      (0 (list 'wheel (car (rd-gen-element remoteterm-tests--wheel-buttons))
               modifiers x y))
      (1 (list 'ignored
               (rd-gen-element
                (list (list 0 1 'frame) (list 0 0 'frame)
                      (list (car (rd-gen-element
                                  remoteterm-tests--wheel-buttons))
                            0 'frame)
                      (list 1 1 'other)))))
      (_ (list 'click (rd-gen-element '(1 2 3 8 9)) modifiers x y
               (remoteterm-tests--random-offset)
               (remoteterm-tests--random-offset))))))

(defun remoteterm-tests--random-offset ()
  "Return a random offset in pixels of a release from its press.
The offsets near `double-click-fuzz' are frequent."
  (let ((size (if (zerop (rd-gen-integer 0 3))
                  (rd-gen-integer 0 40)
                (rd-gen-integer 0 (+ double-click-fuzz 2)))))
    (if (zerop (rd-gen-integer 0 1)) size (- size))))

(defun remoteterm-tests--random-mouse-inputs ()
  "Return a list of random mouse inputs."
  (cl-loop repeat (rd-gen-integer 1 8)
           collect (remoteterm-tests--random-mouse-input)))

(defun remoteterm-tests--text-area-pixel (window fraction-x fraction-y)
  "Return the pixel (X . Y) of WINDOW at the fractions of its text area.
X and Y are relative to the text area."
  (pcase-let ((`(,left ,top ,right ,bottom) (window-inside-pixel-edges window)))
    (cons (floor (* fraction-x (- right left 1)))
          (floor (* fraction-y (- bottom top 1))))))

(defvar remoteterm-tests--mouse-time 0
  "The time of the client of the last mouse input of a test.
The time increases from trial to trial, because keyboard.c compares the
time of a click with the time of the click before, also of an earlier
trial, to find a double click.")

(defun remoteterm-tests--drag-p (window start end)
  "Return non-nil if a press at START and a release at END are a drag.
START and END are pixels of the text area of WINDOW.  This is the rule
of keyboard.c: a release that is `double-click-fuzz' pixels or more
from the press on an axis, or on another buffer position, is a drag."
  (or (>= (abs (- (car end) (car start))) double-click-fuzz)
      (>= (abs (- (cdr end) (cdr start))) double-click-fuzz)
      (not (eql (posn-point (posn-at-x-y (car start) (cdr start) window))
                (posn-point (posn-at-x-y (car end) (cdr end) window))))))

(defun remoteterm-tests--mouse-messages (inputs frame)
  "Return the messages and the expected events of the mouse INPUTS in FRAME.
The value is (MESSAGES . EVENTS), with the expected events in the
form of `remoteterm-tests--event-summary'."
  (let* ((window (frame-selected-window frame))
         (edges (window-inside-pixel-edges window))
         (id (remoteterm-tests--frame-id frame))
         (messages nil)
         (events nil))
    (cl-flet ((button (frame time pixel button modifiers press)
                (push `(button :frame ,frame :time ,time
                               :x ,(+ (nth 0 edges) (car pixel))
                               :y ,(+ (nth 1 edges) (cdr pixel))
                               :button ,button :modifiers ,modifiers
                               :press ,press)
                      messages)))
      (dolist (input inputs)
        (let ((time (cl-incf remoteterm-tests--mouse-time 10000))
              (names (and (integerp (nth 2 input))
                          (remoteterm-tests--modifier-names (nth 2 input)))))
          (pcase input
            (`(ignored (,b ,press ,which))
             (button (if (eq which 'frame) id (+ id 1000)) time '(5 . 5)
                     b 0 press))
            (`(wheel ,b ,modifiers ,x ,y)
             (let ((pixel (remoteterm-tests--text-area-pixel window x y)))
               (button id time pixel b modifiers 1)
               (button id (+ time 50) pixel b modifiers 0)
               (push (list (event-convert-list
                            (append names
                                    (list (alist-get
                                           b remoteterm-tests--wheel-buttons))))
                           pixel time)
                     events)))
            (`(click ,b ,modifiers ,x ,y ,dx ,dy)
             (let* ((start (remoteterm-tests--text-area-pixel window x y))
                    (end (cons (+ (car start) dx) (+ (cdr start) dy)))
                    (mouse (intern (format "mouse-%d" b))))
               (button id time start b modifiers 1)
               (button id (+ time 50) end b modifiers 0)
               (push (list (event-convert-list (append names (list 'down mouse)))
                           start time)
                     events)
               (push (if (remoteterm-tests--drag-p window start end)
                         (list (event-convert-list
                                (append names (list 'drag mouse)))
                               start end time (+ time 50))
                       (list (event-convert-list
                              (append names (list mouse)))))
                     events)))))))
    (cons (nreverse messages) (nreverse events))))

(defun remoteterm-tests--event-summary (event)
  "Return the summary of the mouse EVENT that the model gives.
A drag is (SYMBOL START END START-TIME END-TIME), a press and a wheel
event are (SYMBOL POSITION TIME), and a click is (SYMBOL).  For Emacs,
a wheel event has the `click' modifier, so the test of a wheel comes
first."
  (let ((symbol (car event))
        (start (event-start event))
        (end (event-end event)))
    (cond ((assq (event-basic-type event)
                 (mapcar (lambda (wheel) (list (cdr wheel)))
                         remoteterm-tests--wheel-buttons))
           (list symbol (posn-x-y start) (posn-timestamp start)))
          ((memq 'drag (event-modifiers event))
           (list symbol (posn-x-y start) (posn-x-y end)
                 (posn-timestamp start) (posn-timestamp end)))
          ((memq 'click (event-modifiers event)) (list symbol))
          (t (list symbol (posn-x-y start) (posn-timestamp start))))))

(defun remoteterm-tests--run-mouse-inputs (inputs)
  "Send the mouse INPUTS to a session, and return (EVENTS . EXPECTED).
A key message comes after the inputs, so that each input that gives no
event must also not hold back a later event."
  (remoteterm-tests--with-input-session
   (lambda (frame client-side &optional _server-side)
     (redisplay t)
     (pcase-let ((`(,messages . ,expected)
                  (remoteterm-tests--mouse-messages inputs frame)))
       (process-send-string
        client-side
        (concat (mapconcat #'remote-display--encode messages)
                (remote-display--encode
                 `(key :frame ,(remoteterm-tests--frame-id frame) :time 0
                       :keysym ?z :character ?z :modifiers 0))))
       (let ((events (remoteterm-tests--read-events (1+ (length expected)))))
         (cons (mapcar (lambda (event)
                         (if (eq event ?z) event
                           (remoteterm-tests--event-summary event)))
                       events)
               (append expected (list ?z))))))))

(rd-daemon-deftest remoteterm-tests-mouse-events
  "A button message gives the click or the wheel event of Emacs.
A press of a button that is not a wheel button gives a down event at
its position and time.  The release gives a click, or a drag from the
press, as `remoteterm-tests--drag-p' says.  A press of a button from 4
to 7 gives a wheel event at its position and time, and its release
gives no event.  Each event has the modifiers of the message.  The
button 0 and a frame that does not exist give no event."
  (let ((rd-gen-trials 30))
    (rd-gen-for-all
     #'remoteterm-tests--random-mouse-inputs
     (lambda (inputs)
       (let ((result (remoteterm-tests--run-mouse-inputs inputs)))
         (should (equal (car result) (cdr result))))))))

(defun remoteterm-tests--random-motions ()
  "Return a list of random motions: (FRAME X Y).
FRAME is `frame' or `other', for a frame that does not exist.  X and Y
can be negative, or far outside the frame."
  (cl-loop repeat (rd-gen-integer 1 10)
           collect (list (if (zerop (rd-gen-integer 0 3)) 'other 'frame)
                         (rd-gen-integer -5000 5000)
                         (rd-gen-integer -5000 5000))))

(defun remoteterm-tests--run-motions (motions)
  "Send MOTIONS to a session, and return the pixel position of the mouse.
The value is (FRAME-P X . Y), with FRAME-P non-nil if the position is
in the frame of the session, or nil if Emacs gives no position."
  (remoteterm-tests--with-input-session
   (lambda (frame client-side &optional _server-side)
     (process-send-string
      client-side
      (mapconcat
       (lambda (motion)
         (remote-display--encode
          `(motion :frame ,(if (eq (nth 0 motion) 'frame)
                               (remoteterm-tests--frame-id frame)
                             (+ 1000 (remoteterm-tests--frame-id frame)))
                   :time 5 :x ,(nth 1 motion) :y ,(nth 2 motion))))
       motions))
     (let ((last (car (last (cl-remove 'other motions :key #'car)))))
       (with-timeout (5)
         (while (and last
                     (not (equal (cdr (mouse-pixel-position))
                                 (cons (nth 1 last) (nth 2 last)))))
           (accept-process-output nil 0.05)))
       (accept-process-output nil 0.1)
       (let ((position (mouse-pixel-position)))
         (and (cadr position)
              (cons (eq (car position) frame) (cdr position))))))))

(rd-daemon-deftest remoteterm-tests-mouse-motions
  "After motion messages, the mouse position is the position of the last
message for a frame of the session.  A motion message for a frame that
does not exist changes nothing."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     #'remoteterm-tests--random-motions
     (lambda (motions)
       (let ((last (car (last (cl-remove 'other motions :key #'car)))))
         (should (equal (remoteterm-tests--run-motions motions)
                        (and last (cons t (cons (nth 1 last)
                                                (nth 2 last)))))))))))

(defun remoteterm-tests--deleted-motion-frame-case ()
  "Move the mouse in a second frame, delete it, and return its position.
Return t if the position has no deleted frame."
  (remoteterm-tests--with-input-session
   (lambda (frame client-side &optional _server-side)
     (let ((other (remoteterm-tests--make-frame "remote-input" nil)))
       (process-send-string
        client-side
        (remote-display--encode
         `(motion :frame ,(remoteterm-tests--frame-id other) :time 5
                  :x 3 :y 4)))
       (with-timeout (5)
         (while (not (eq (car (mouse-pixel-position)) other))
           (accept-process-output nil 0.05)))
       (delete-frame other)
       (select-frame frame)
       (and (frame-live-p (car (mouse-pixel-position))) t)))))

(ert-deftest remoteterm-tests-mouse-deleted-frame ()
  "The mouse position has no frame that is deleted."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (eq (remoteterm-tests--daemon-call
                 'remoteterm-tests--deleted-motion-frame-case)
                t))))

(defun remoteterm-tests--mouse-face-case ()
  "Move the mouse onto a text with a `mouse-face', and return the frame
of the mouse highlight of the session."
  (remoteterm-tests--with-input-session
   (lambda (frame client-side &optional _server-side)
     (let ((buffer (get-buffer-create "remote-mouse-face")))
       (with-current-buffer buffer
         (erase-buffer)
         (insert (propertize "mouse" 'mouse-face 'highlight)))
       (set-window-buffer (frame-selected-window frame) buffer)
       (redisplay t)
       (let ((edges (window-inside-pixel-edges (frame-selected-window frame))))
         (process-send-string
          client-side
          (remote-display--encode
           `(motion :frame ,(remoteterm-tests--frame-id frame) :time 5
                    :x ,(1+ (nth 0 edges)) :y ,(1+ (nth 1 edges)))))
         (with-timeout (5)
           (while (not (remote-display--mouse-frame "remote-input"))
             (accept-process-output nil 0.05)))
         (prog1 (eq (remote-display--mouse-frame "remote-input") frame)
           (kill-buffer buffer)))))))

(ert-deftest remoteterm-tests-mouse-face ()
  "A motion message onto a text with a `mouse-face' highlights the text."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (eq (remoteterm-tests--daemon-call
                 'remoteterm-tests--mouse-face-case)
                t))))

(defun remoteterm-tests--double-click-case ()
  "Send two clicks at one place within `double-click-time', return events."
  (remoteterm-tests--with-input-session
   (lambda (frame client-side &optional _server-side)
     (redisplay t)
     (let* ((edges (window-inside-pixel-edges (frame-selected-window frame)))
            (x (+ (nth 0 edges) 10))
            (y (+ (nth 1 edges) 10)))
       (process-send-string
        client-side
        (mapconcat
         (lambda (step)
           (remote-display--encode
            `(button :frame ,(remoteterm-tests--frame-id frame)
                     :time ,(car step) :x ,x :y ,y :button 1 :modifiers 0
                     :press ,(cdr step))))
         '((1000 . 1) (1050 . 0) (1100 . 1) (1150 . 0))))
       (mapcar #'car (remoteterm-tests--read-events 4))))))

(ert-deftest remoteterm-tests-mouse-double-click ()
  "Two clicks at one place in a short time give a double click."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--double-click-case)
                   '(down-mouse-1 mouse-1 double-down-mouse-1
                                  double-mouse-1)))))

(defun remoteterm-tests--mouse-position-case ()
  "Move the mouse of the client, and return the pixel position of Emacs."
  (remoteterm-tests--with-input-session
   (lambda (frame client-side &optional _server-side)
     (process-send-string
      client-side
      (remote-display--encode
       `(motion :frame ,(remoteterm-tests--frame-id frame) :time 5
                :x 37 :y 23)))
     (with-timeout (5)
       (while (not (equal (cdr (mouse-pixel-position)) '(37 . 23)))
         (accept-process-output nil 0.05)))
     (let ((position (mouse-pixel-position)))
       (list (eq (car position) frame) (cdr position))))))

(ert-deftest remoteterm-tests-mouse-position ()
  "After a motion message, the mouse position is the position of it."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--mouse-position-case)
                   '(t (37 . 23))))))

;; With `track-mouse', Emacs asks the mouse_position_hook of the
;; terminal for the position, and makes a `mouse-movement' event.

(defun remoteterm-tests--track-mouse-case ()
  "Move the mouse in `track-mouse', and return the event and its position."
  (remoteterm-tests--with-input-session
   (lambda (frame client-side &optional _server-side)
     (redisplay t)
     (let* ((window (frame-selected-window frame))
            (edges (window-inside-pixel-edges window)))
       (process-send-string
        client-side
        (remote-display--encode
         `(motion :frame ,(remoteterm-tests--frame-id frame) :time 5
                  :x ,(+ (nth 0 edges) 11) :y ,(+ (nth 1 edges) 7))))
       (let ((event (track-mouse
                      (with-timeout (5 'timeout)
                        (let ((event nil))
                          (while (or (null event)
                                     (eq (car-safe event) 'switch-frame))
                            (setq event (read-event nil nil 0.1)))
                          event)))))
         (list (car-safe event)
               (eq (posn-window (event-start event)) window)
               (posn-x-y (event-start event))
               ;; One motion gives one event: the hook clears the
               ;; mark of the motion.
               (track-mouse (read-event nil nil 0.3))))))))

(ert-deftest remoteterm-tests-track-mouse ()
  "In `track-mouse', a motion message gives a `mouse-movement' event."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--track-mouse-case)
                   '(mouse-movement t (11 . 7) nil)))))

;;;; Help events and tip frames

;; A motion message gives a help event when the help text under the
;; mouse is not nil, or was not nil before, as in pgtkterm.c.  A frame
;; with the parameter `remote-tip' is transparent to input: its
;; parent gets each motion, button and wheel input at the same place.

(defconst remoteterm-tests--help "The help"
  "The `help-echo' text of the tests of help events.")

(defmacro remoteterm-tests--with-help-root (root &rest body)
  "Bind ROOT to a new root frame that shows a help text, and evaluate BODY.
ROOT is as in `remoteterm-tests--with-place-root'.  Its window shows
the buffer of `remoteterm-tests--help-buffer'."
  (declare (indent 1))
  `(remoteterm-tests--with-place-root ,root
     (let ((buffer (remoteterm-tests--help-buffer)))
       (unwind-protect
           (progn
             (select-frame ,root)
             (set-window-buffer (frame-first-window ,root) buffer)
             (redisplay t)
             ,@body)
         (kill-buffer buffer)))))

(rd-daemon-deftest remoteterm-tests-help-on-motion
  "A motion over a text with a `help-echo' shows the help (law H1).
A motion over a plain text after it calls `show-help-function' with
nil, so the help goes away."
  (remoteterm-tests--with-help-root root
    (let ((rd-gen-trials 20))
      (rd-gen-for-all
       (lambda () (list (rd-gen-integer 0 39) (rd-gen-integer 0 4)
                        (rd-gen-integer 1 10)))
       (pcase-lambda (`(,column ,plain-column ,plain-line))
         (should (equal (remoteterm-tests--helps-of-motions
                         (list (remoteterm-tests--cell-point root column 0)
                               (remoteterm-tests--cell-point
                                root plain-column plain-line)))
                        (list remoteterm-tests--help nil))))))))

(rd-daemon-deftest remoteterm-tests-motion-into-child-clears-highlight
  "A motion from a frame into its child clears the mouse highlight (law H2)."
  (remoteterm-tests--with-help-root root
    (let ((child (remoteterm-tests--plain-child root '((left . 30)
                                                      (top . 20))))
          (rd-gen-trials 10))
      (rd-gen-for-all
       (lambda () (list (rd-gen-integer 0 39) (rd-gen-integer 0 20)
                        (rd-gen-integer 0 20)))
       (pcase-lambda (`(,column ,x ,y))
         (remoteterm-tests--helps-of-motions
          (list (remoteterm-tests--cell-point root column 0)))
         (should (eq (remote-display--mouse-face-window
                      remoteterm-tests--place-display)
                     (frame-first-window root)))
         (remoteterm-tests--helps-of-motions (list (list child x y)))
         (should-not (remote-display--mouse-face-window
                      remoteterm-tests--place-display)))))))

(rd-daemon-deftest remoteterm-tests-tip-transparent
  "A tip frame is transparent to input (law H3).
For a child T of R with the parameter `remote-tip', a motion, button or
wheel input for T at X, Y has the effect of the same input for R at
X + left of T, Y + top of T.  A sum outside the range of int32 stops at
the limit of the range."
  (remoteterm-tests--with-help-root root
    (let* ((tip (remoteterm-tests--plain-child root '((remote-tip . t)
                                                     (left . 30)
                                                     (top . 20))))
           (position (frame-position tip))
           (time 0)
           (rd-gen-trials 40))
      (rd-gen-for-all
       #'remoteterm-tests--tip-input
       (pcase-lambda (`(,kind ,x ,y))
         (should (equal (remoteterm-tests--input-effect
                         tip kind x y (cl-incf time 10000))
                        (remoteterm-tests--input-effect
                         root kind
                         (remoteterm-tests--int32 (+ x (car position)))
                         (remoteterm-tests--int32 (+ y (cdr position)))
                         (cl-incf time 10000)))))))))

(rd-daemon-deftest remoteterm-tests-tip-keeps-help
  "A motion in a tip over the help text of its parent keeps the help (law H4).
Thus `tooltip-show-help' does not hide the tip when the mouse moves
onto it."
  (remoteterm-tests--with-help-root root
    (let* ((start (remoteterm-tests--cell-point root 2 0))
           (width (frame-char-width root))
           (height (frame-char-height root))
           (tip (remoteterm-tests--plain-child
                 root `((remote-tip . t)
                        (left . ,(- (nth 1 start) (/ width 2)))
                        (top . ,(- (nth 2 start) (/ height 2))))))
           (rd-gen-trials 20))
      (rd-gen-for-all
       (lambda () (list (rd-gen-integer 0 (1- (* 8 width)))
                        (rd-gen-integer 0 (1- height))))
       (pcase-lambda (`(,x ,y))
         (should (equal (last (remoteterm-tests--helps-of-motions
                               (list (remoteterm-tests--cell-point root 0 5)
                                     (remoteterm-tests--cell-point root 0 0)
                                     (list tip x y)))
                              2)
                        (list remoteterm-tests--help
                              remoteterm-tests--help))))))))

(defun remoteterm-tests--help-buffer ()
  "Return a new buffer with a help text on line 0 and plain text after it.
Line 0 has 40 characters with the `help-echo' `remoteterm-tests--help'
and a `mouse-face'.  Each of the next 10 lines has the text \"plain\"."
  (let ((buffer (generate-new-buffer "remote-help")))
    (with-current-buffer buffer
      (insert (propertize (make-string 40 ?h)
                          'help-echo remoteterm-tests--help
                          'mouse-face 'highlight)
              "\n")
      (dotimes (_ 10)
        (insert "plain\n")))
    buffer))

(defun remoteterm-tests--plain-child (parent parameters)
  "Make a visible child frame of PARENT with PARAMETERS, and return it.
Its window shows an empty buffer, so it has no help text.  Redisplay
the frames."
  (let ((child (remoteterm-tests--place-child parent parameters)))
    (set-window-buffer (frame-first-window child)
                       (get-buffer-create " remote-empty"))
    (redisplay t)
    child))

(defun remoteterm-tests--cell-point (frame column line)
  "Return (FRAME X Y), the middle of a cell of the first window of FRAME.
The cell is at COLUMN and LINE of the text area."
  (let ((edges (window-inside-pixel-edges (frame-first-window frame)))
        (width (frame-char-width frame))
        (height (frame-char-height frame)))
    (list frame
          (+ (nth 0 edges) (* column width) (/ width 2))
          (+ (nth 1 edges) (* line height) (/ height 2)))))

(defun remoteterm-tests--helps-of-motions (points)
  "Send a motion message to each of POINTS, and return the helps.
Each point is (FRAME X Y).  The value is the list of the arguments of
each call of `show-help-function', with no text properties."
  (let* ((helps nil)
         (show-help-function
          (lambda (help)
            (push (and help (substring-no-properties help)) helps))))
    (pcase-dolist (`(,frame ,x ,y) points)
      (remoteterm-tests--input frame 'motion x y 5)
      (remoteterm-tests--handle-events))
    (nreverse helps)))

(defun remoteterm-tests--input (frame type x y time &rest fields)
  "Give the session of FRAME a TYPE message for FRAME at X, Y and TIME.
FIELDS are the other fields of the message, as a plist."
  (remote-display--input
   (terminal-name (frame-terminal frame))
   (remote-display--encode
    `(,type :frame ,(remoteterm-tests--frame-id frame) :time ,time
            :x ,x :y ,y ,@fields))))

(defun remoteterm-tests--handle-events ()
  "Read the events of Emacs until no event comes.
`read-event' calls `show-help-function' for each help event."
  (while (read-event nil nil 0.05)))

(defun remoteterm-tests--tip-input ()
  "Return a random input for a tip frame: (KIND X Y).
KIND is `motion', `button' or `wheel'.  X and Y are often from 0 to
39, in the tip or near it, and sometimes at a limit of int32."
  (cl-flet ((coordinate ()
              (if (zerop (rd-gen-random 4))
                  (rd-gen-int32)
                (rd-gen-integer 0 39))))
    (list (rd-gen-element '(motion button wheel))
          (coordinate) (coordinate))))

(defun remoteterm-tests--input-effect (frame kind x y time)
  "Give FRAME the input KIND at X, Y and TIME, and return its effect.
For `motion', the effect is the mouse position.  For `button', it is
the summaries of the events of a press and a release of button 1.  For
`wheel', it is the summary of the event of a press of button 4."
  (pcase kind
    ('motion
     (remoteterm-tests--input frame 'motion x y time)
     (mouse-pixel-position))
    ('button
     (dolist (press '(1 0))
       (remoteterm-tests--input frame 'button x y time
                                :button 1 :modifiers 0 :press press))
     (mapcar #'remoteterm-tests--position-summary
             (remoteterm-tests--read-events 2)))
    ('wheel
     (remoteterm-tests--input frame 'button x y time
                              :button 4 :modifiers 0 :press 1)
     (mapcar #'remoteterm-tests--position-summary
             (remoteterm-tests--read-events 1)))))

(defun remoteterm-tests--position-summary (event)
  "Return the type, the window, the area and the position of EVENT."
  (let ((position (event-start event)))
    (list (event-basic-type event) (posn-window position)
          (posn-area position) (posn-x-y position))))

(defun remoteterm-tests--int32 (n)
  "Return N, or the limit of the range of int32 that is nearest to N."
  (max (- (ash 1 31)) (min n (1- (ash 1 31)))))

;;;; Tooltips

;; A tooltip is a child frame of the root frame under the mouse, with
;; the parameter `remote-tip'.  `x-show-tip' of lisp/term/remote-tip.el
;; makes it, and `x-hide-tip' deletes it.

(declare-function remote-display-tip-position "remote-tip"
                  (mouse size root-size offsets parms))

(defvar remote-display-tip-default-offsets)

(ert-deftest remoteterm-tests-tip-position ()
  "The place of a tooltip keeps it in the root and off the mouse (law T1).
On each axis: an integer left or top in the parameters gives that
exact position.  Else a tooltip that is not larger than the root is in
the root.  If the tooltip fits in the root before or after the mouse
at the offset, it does not contain the mouse.  If it fits on neither
side, it starts at the far edge of the root, or at 0 if it is larger
than the root."
  (skip-unless (featurep 'remote))
  (rd-gen-for-all
   (lambda () (list (rd-gen-tip-axis) (rd-gen-tip-axis)))
   (pcase-lambda (`(,x-case ,y-case))
     (pcase-let* ((`(,mouse-x ,width ,root-width ,dx ,left) x-case)
                  (`(,mouse-y ,height ,root-height ,dy ,top) y-case)
                  (`(,x . ,y)
                   (remote-display-tip-position
                    (cons mouse-x mouse-y) (cons width height)
                    (cons root-width root-height) (cons dx dy)
                    (append (and left `((left . ,left)))
                            (and top `((top . ,top)))))))
       (remoteterm-tests--check-tip-axis
        x-case (car remote-display-tip-default-offsets) x)
       (remoteterm-tests--check-tip-axis
        y-case (cdr remote-display-tip-default-offsets) y)))))

(defun remoteterm-tests--check-tip-axis (case default-offset start)
  "Check the law T1 for START, the place of a tooltip on one axis.
CASE is a value of `rd-gen-tip-axis'.  DEFAULT-OFFSET is the offset
of the axis when the offset of CASE is nil."
  (pcase-let ((`(,mouse ,size ,limit ,offset ,explicit) case))
    (if explicit
        (should (= start explicit))
      (let ((offset (or offset default-offset)))
        (when (<= size limit)
          (should (<= 0 start (- limit size))))
        (if (or (<= 0 (+ mouse offset) (- limit size))
                (<= 0 (- mouse offset size) (- limit size)))
            (should-not (<= start mouse (+ start size -1)))
          (should (= start (max 0 (- limit size)))))))))

(rd-daemon-deftest remoteterm-tests-tip-show
  "After `x-show-tip', the root has one visible tooltip with the text (law T2).
The text area of the tooltip is as wide as the longest line, but not
wider than `x-max-tooltip-size'.  It is as high as the lines, but not
higher than `x-max-tooltip-size'.  A line that wraps can make it
higher, up to `x-max-tooltip-size'.  The session has no mouse
position, so the place of the tooltip is that of law T1 for the mouse
at the top-left corner of the root."
  (remoteterm-tests--with-place-root root
    (let ((rd-gen-trials 20))
      (rd-gen-for-all
       (lambda ()
         (list (rd-gen-list (lambda () (rd-gen-tip-text 4 100)) 2)
               (rd-gen-tip-text 4 100)
               (cons (rd-gen-integer 5 80) (rd-gen-integer 2 40))))
       (pcase-lambda (`(,before ,text ,max-size))
         (let ((x-max-tooltip-size max-size))
           (dolist (other before)
             (x-show-tip other root))
           (x-show-tip text root)
           (remoteterm-tests--check-tip-place
            (remoteterm-tests--check-tip root text) root '(0 . 0))))))))

(rd-daemon-deftest remoteterm-tests-tip-settings
  "A tooltip has no bars and no fullscreen state (law T3).
The frame defaults and the bar modes do not change this, and the size
is that of law T2."
  (let ((rd-gen-trials 10))
    (rd-gen-for-all
     (lambda () (list (rd-gen-tip-text 4 100) (rd-gen-frame-defaults)
                      (rd-gen-element '(nil t)) (rd-gen-element '(nil t))))
     (pcase-lambda (`(,text ,defaults ,menu-bar ,tab-bar))
       (remoteterm-tests--with-place-root root
         (remoteterm-tests--with-bar-modes
          menu-bar tab-bar
          (lambda ()
            (let ((default-frame-alist defaults))
              (x-show-tip text root)
              (let ((tip (remoteterm-tests--check-tip root text)))
                (should (equal (mapcar (lambda (parameter)
                                         (frame-parameter tip parameter))
                                       '(menu-bar-lines tab-bar-lines
                                         tool-bar-lines fullscreen
                                         vertical-scroll-bars
                                         horizontal-scroll-bars))
                               '(0 0 0 nil nil nil))))))))))))

(defun remoteterm-tests--with-bar-modes (menu-bar tab-bar function)
  "Call FUNCTION with `menu-bar-mode' MENU-BAR and `tab-bar-mode' TAB-BAR.
Then give the two modes their old states again."
  (let ((old-menu-bar menu-bar-mode)
        (old-tab-bar tab-bar-mode))
    (unwind-protect
        (progn
          (menu-bar-mode (if menu-bar 1 -1))
          (tab-bar-mode (if tab-bar 1 -1))
          (funcall function))
      (menu-bar-mode (if old-menu-bar 1 -1))
      (tab-bar-mode (if old-tab-bar 1 -1)))))

(rd-daemon-deftest remoteterm-tests-tip-hide
  "`x-hide-tip' returns t once, and then nil (law T4).
This is true after one or more calls of `x-show-tip'.  After it, the
session has no tooltip frame."
  (let ((rd-gen-trials 10))
    (rd-gen-for-all
     (lambda () (cons (rd-gen-tip-text 4 40)
                      (rd-gen-list (lambda () (rd-gen-tip-text 4 40)) 3)))
     (lambda (texts)
       (remoteterm-tests--with-place-root root
         (dolist (text texts)
           (x-show-tip text root))
         (should (equal (list (x-hide-tip) (x-hide-tip)) '(t nil)))
         (should-not (remoteterm-tests--tip-frames)))))))

(rd-daemon-deftest remoteterm-tests-tip-under-child
  "A tooltip goes to the root of the frame under the mouse (law T9).
The mouse is at a point of the last frame of a chain of 0 to 3 nested
child frames of a root.  The tooltip is a child of the root.  Its place
is that of law T1 for the mouse position in the root: the point plus
the position of each frame of the chain."
  (let ((rd-gen-trials 10))
    (rd-gen-for-all
     (lambda () (list (rd-gen-child-chain 3)
                      (rd-gen-random 1000) (rd-gen-random 1000)))
     (pcase-lambda (`(,positions ,x-seed ,y-seed))
       (remoteterm-tests--with-place-root root
         (let* ((last (car (last (cons root (remoteterm-tests--child-chain
                                             root positions)))))
                (x (mod x-seed (frame-pixel-width last)))
                (y (mod y-seed (frame-pixel-height last))))
           (remoteterm-tests--input last 'motion x y 5)
           (remoteterm-tests--handle-events)
           (x-show-tip "tip" root)
           (remoteterm-tests--check-tip-place
            (remoteterm-tests--check-tip root "tip") root
            (cons (apply #'+ x (mapcar #'car positions))
                  (apply #'+ y (mapcar #'cdr positions))))))))))

(rd-daemon-deftest remoteterm-tests-tip-no-mouse
  "With no mouse position, a tooltip goes to the root of FRAME (law T10).
FRAME is the last frame of a chain of 0 to 3 nested child frames of a
root, and the session had no motion.  The tooltip is a child of the
root.  Its place is that of law T1 for the mouse at the top-left
corner of the root."
  (let ((rd-gen-trials 10))
    (rd-gen-for-all
     (lambda () (rd-gen-child-chain 3))
     (lambda (positions)
       (remoteterm-tests--with-place-root root
         (let ((frame (car (last (cons root (remoteterm-tests--child-chain
                                             root positions))))))
           (x-show-tip "tip" frame)
           (remoteterm-tests--check-tip-place
            (remoteterm-tests--check-tip root "tip") root '(0 . 0))))))))

;; Laws T5 and T6 use a few examples and not random inputs, because
;; they wait on timers and on real time.  Each case takes a long time.

(rd-daemon-deftest remoteterm-tests-tip-timeout
  "A small TIMEOUT hides the tooltip (law T5).
The TIMEOUT `most-positive-fixnum' of mouse.el gives no error, and the
tooltip stays."
  (remoteterm-tests--for-each-case
   `((0.1 0) (,most-positive-fixnum 1))
   (pcase-lambda (`(,timeout ,count))
     (remoteterm-tests--with-place-root root
       (x-show-tip "timeout" root nil timeout)
       (with-timeout (1)
         (while (/= (length (remoteterm-tests--tip-frames)) count)
           (accept-process-output nil 0.05)))
       (remoteterm-tests--handle-events)
       (should (= (length (remoteterm-tests--tip-frames)) count))))))

(rd-daemon-deftest remoteterm-tests-tip-mode
  "With `tooltip-mode', a motion over a help text shows a tooltip (law T6).
A motion into the tooltip after that does not delete it."
  (remoteterm-tests--for-each-case
   '((2 2) (10 5))
   (pcase-lambda (`(,column ,line))
     (remoteterm-tests--with-place-root root
       (let ((buffer (remoteterm-tests--all-help-buffer))
             (tooltip-delay 0)
             (tooltip-short-delay 0)
             (tooltip-help-message nil))
         (unwind-protect
             (progn
               (tooltip-mode 1)
               (select-frame root)
               (set-window-buffer (frame-first-window root) buffer)
               (redisplay t)
               (pcase-let ((`(,_ ,x ,y)
                            (remoteterm-tests--cell-point root column line)))
                 (remoteterm-tests--input root 'motion x y 5))
               (remoteterm-tests--handle-events)
               (let ((tips (remoteterm-tests--tip-frames)))
                 (should (= (length tips) 1))
                 (remoteterm-tests--input (car tips) 'motion 1 1 6)
                 (remoteterm-tests--handle-events)
                 (should (equal (remoteterm-tests--tip-frames) tips))))
           (tooltip-mode -1)
           (kill-buffer buffer)))))))

(ert-deftest remoteterm-tests-tip-hide-calls-lisp ()
  "The C function `x-hide-tip' returns the value of the Lisp hide (law T7)."
  (skip-unless (featurep 'remote))
  (cl-letf (((symbol-function 'remote-display--hide-tip)
             (lambda () 'hidden)))
    (should (eq (x-hide-tip) 'hidden))))

(ert-deftest remoteterm-tests-frame-border-width ()
  "A new frame can have a border-width parameter (law T8).
`tooltip-frame-parameters' give one.  The client draws no outer
border, so the border width of the frame and the outer border width
of its geometry stay 0."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-border"
    (remoteterm-tests--with-frame frame "remote-border" '((border-width . 1))
      (should (eql (frame-parameter frame 'border-width) 0))
      (should (eql (alist-get 'outer-border-width (frame-geometry frame))
                   0)))))

(defun remoteterm-tests--check-tip (root text)
  "Check that ROOT has one visible tooltip that shows TEXT, and return it.
Check the size of law T2."
  (let ((tips (seq-filter (lambda (frame)
                            (and (eq (frame-parent frame) root)
                                 (frame-visible-p frame)))
                          (remoteterm-tests--tip-frames))))
    (should (equal tips (remoteterm-tests--tip-frames)))
    (should (= (length tips) 1))
    (let ((tip (car tips)))
      (should (equal (with-current-buffer
                         (window-buffer (frame-root-window tip))
                       (buffer-string))
                     text))
      (remoteterm-tests--check-tip-size tip text)
      tip)))

(defun remoteterm-tests--check-tip-place (tip root mouse)
  "Check that the tooltip TIP of ROOT has the place of law T1.
MOUSE is the mouse position (X . Y) in ROOT.  The call of `x-show-tip'
gave no offsets and no parameters."
  (should (equal (frame-position tip)
                 (remote-display-tip-position
                  mouse
                  (cons (frame-pixel-width tip) (frame-pixel-height tip))
                  (cons (frame-native-width root) (frame-native-height root))
                  nil nil))))

(defun remoteterm-tests--child-chain (root positions)
  "Make a chain of nested child frames in ROOT, and return it.
The first frame is a child of ROOT, and each other frame is a child of
the frame before it.  POSITIONS gives the position (LEFT . TOP) of each
frame in its parent."
  (let ((parent root))
    (mapcar (pcase-lambda (`(,left . ,top))
              (setq parent (remoteterm-tests--plain-child
                            parent `((left . ,left) (top . ,top)))))
            positions)))

(defun remoteterm-tests--check-tip-size (tip text)
  "Check that the text area of the tooltip TIP fits TEXT (law T2)."
  (let* ((window (frame-root-window tip))
         (lines (string-lines text))
         (longest (apply #'max (mapcar #'length lines)))
         (max-width (* (car x-max-tooltip-size) (frame-char-width tip)))
         (max-height (* (cdr x-max-tooltip-size) (frame-char-height tip)))
         (text-height (min max-height
                           (* (length lines) (frame-char-height tip))))
         (height (window-body-height window t)))
    (should (= (window-body-width window t)
               (min max-width (* longest (frame-char-width tip)))))
    (if (<= longest (car x-max-tooltip-size))
        (should (= height text-height))
      (should (<= text-height height max-height)))))

(defun remoteterm-tests--tip-frames ()
  "Return the live tooltip frames of `remoteterm-tests--place-display'."
  (seq-filter (lambda (frame) (frame-parameter frame 'remote-tip))
              (remoteterm-tests--place-frames)))

(defun remoteterm-tests--all-help-buffer ()
  "Return a new buffer whose whole text has the same `help-echo'.
It has 20 lines of 40 characters, with the help text
`remoteterm-tests--help'."
  (let ((buffer (generate-new-buffer "remote-all-help")))
    (with-current-buffer buffer
      (dotimes (_ 20)
        (insert (make-string 40 ?h) "\n"))
      (put-text-property (point-min) (point-max)
                         'help-echo remoteterm-tests--help))
    buffer))

;;;; Focus and size input

(defun remoteterm-tests--z-key (frame)
  "Return the bytes of a key message for ?z in FRAME.
A test sends it after other messages, and reads events until it comes."
  (remote-display--encode
   `(key :frame ,(remoteterm-tests--frame-id frame) :time 0
         :keysym ?z :character ?z :modifiers 0)))

(defun remoteterm-tests--random-focus-messages ()
  "Return a list of random focus messages: (TARGET IN).
TARGET is `first' or `second', for a frame of the session, or `other',
for a frame that does not exist.  IN is 1 or 0."
  (cl-loop repeat (rd-gen-integer 1 10)
           collect (list (rd-gen-element '(first second other))
                         (rd-gen-integer 0 1))))

(defun remoteterm-tests--focus-model (messages)
  "Return (FOCUS . EVENTS) for MESSAGES.
FOCUS is the target that has the focus after MESSAGES, or nil.  EVENTS
is the list of the focus events, each (in TARGET) or (out TARGET).  A
focus-in of a frame with no focus gives a focus-out of the old focus
frame and a focus-in.  A focus-out of the frame with the focus gives
a focus-out.  Another message, or a message for `other', gives no
event."
  (let ((focus nil)
        (events nil))
    (pcase-dolist (`(,target ,in) messages)
      (cond ((eq target 'other))
            ((and (= in 1) (not (eq focus target)))
             (when focus
               (push (list 'out focus) events))
             (push (list 'in target) events)
             (setq focus target))
            ((and (= in 0) (eq focus target))
             (push (list 'out target) events)
             (setq focus nil))))
    (cons focus (nreverse events))))

(defun remoteterm-tests--run-focus-messages (messages)
  "Send the focus MESSAGES to a session with two frames.
Return (FOCUS STATES EVENTS).  FOCUS is the list of the focus frame
and the highlight frame of the session, and STATES is the list of the
`frame-focus-state' of each frame.  EVENTS is the list of the focus
events that Emacs handled, as in `remoteterm-tests--focus-model'.  The
value has the targets of MESSAGES in place of the frames."
  (remoteterm-tests--with-input-session
   (lambda (frame client-side &optional _server-side)
     (let* ((second (remoteterm-tests--make-frame "remote-input" nil))
            (frames `((first . ,frame) (second . ,second)))
            (events nil)
            (special-event-map (copy-keymap special-event-map)))
       (dolist (kind '((focus-in in handle-focus-in)
                       (focus-out out handle-focus-out)))
         (define-key special-event-map (vector (car kind))
                     (lambda (event)
                       (interactive "e")
                       (push (list (nth 1 kind)
                                   (car (rassq (nth 1 event) frames)))
                             events)
                       (funcall (nth 2 kind) event))))
       (unwind-protect
           (progn
             (process-send-string
              client-side
              (concat
               (mapconcat
                (lambda (message)
                  (remote-display--encode
                   `(focus :frame
                           ,(if (eq (car message) 'other)
                                (+ 1000 (remoteterm-tests--frame-id second))
                              (remoteterm-tests--frame-id
                               (alist-get (car message) frames)))
                           :in ,(cadr message))))
                messages)
               (remoteterm-tests--z-key frame)))
             (remoteterm-tests--read-events 1)
             (list (mapcar (lambda (focus) (car (rassq focus frames)))
                           (remote-display--focus-frames "remote-input"))
                   (mapcar (lambda (target)
                             (frame-focus-state (alist-get target frames)))
                           '(first second))
                   (reverse events)))
         (delete-frame second))))))

(rd-daemon-deftest remoteterm-tests-focus-events
  "A focus-in message gives its frame the focus of the session.  A
focus-out message of the frame with the focus leaves the session with
no focus frame.  Emacs sees each change as a `focus-in' or a
`focus-out' event, so at most one frame of the session has the focus
state t.  A message that does not change the focus frame, or a
message for a frame that does not exist, gives no event."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     #'remoteterm-tests--random-focus-messages
     (lambda (messages)
       (pcase-let ((`(,focus . ,events)
                    (remoteterm-tests--focus-model messages)))
         (should (equal (remoteterm-tests--run-focus-messages messages)
                        (list (list focus focus)
                              (list (eq focus 'first)
                                    (eq focus 'second))
                              events))))))))

(defun remoteterm-tests--focus-cursor-case ()
  "Give the frame the focus, take it away, and give it back.
Return the kind of the last cursor that the client got after each
step.  No step does a redisplay."
  (remoteterm-tests--with-input-session
   (lambda (frame client-side &optional _server-side)
     (let ((chunks nil)
           (id (remoteterm-tests--frame-id frame)))
       (redisplay t)
       (set-process-filter client-side
                           (lambda (_process string) (push string chunks)))
       (mapcar
        (lambda (in)
          (setq chunks nil)
          (process-send-string
           client-side (remote-display--encode `(focus :frame ,id :in ,in)))
          (with-timeout (5)
            (while (not (eq (car (remote-display--focus-frames
                                  "remote-input"))
                            (and (= in 1) frame)))
              (accept-process-output nil 0.05)))
          (accept-process-output nil 0.2)
          (car (remoteterm-tests--last-cursor
                (apply #'concat (reverse chunks)))))
        '(1 0 1))))))

(ert-deftest remoteterm-tests-focus-cursor ()
  "A change of the focus draws the cursor again at once.
With the focus, the cursor is a box.  With no focus, it is hollow."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--focus-cursor-case)
                   (mapcar (lambda (type)
                             (alist-get type remoteterm-tests--cursor-kinds))
                           '(box hollow box))))))

(ert-deftest remoteterm-tests-focus-redirect ()
  "The highlight frame of a session is the frame of the focus redirection.
A redirection to a frame of another session or terminal does not move
the highlight frame."
  (skip-unless (featurep 'remote-test))
  (remoteterm-tests--with-display "remote-focus"
    (remoteterm-tests--with-frame focused "remote-focus" nil
      (remoteterm-tests--with-frame target "remote-focus" nil
        (remote-display--focus focused)
        (redirect-frame-focus focused target)
        (should (equal (remote-display--focus-frames "remote-focus")
                       (list focused target)))
        (let ((foreign (seq-find (lambda (frame)
                                   (not (eq (frame-terminal frame)
                                            (frame-terminal focused))))
                                 (frame-list))))
          (should foreign)
          (redirect-frame-focus focused foreign)
          (should (equal (remote-display--focus-frames "remote-focus")
                         (list focused focused))))
        (redirect-frame-focus focused nil)
        (should (equal (remote-display--focus-frames "remote-focus")
                       (list focused focused)))
        ;; Emacs keeps a redirection to a frame that it deletes.
        (let ((gone (remoteterm-tests--make-frame "remote-focus" nil)))
          (redirect-frame-focus focused gone)
          (delete-frame gone)
          (remote-display--focus focused)
          (should (equal (remote-display--focus-frames "remote-focus")
                         (list focused focused))))))))

(defconst remoteterm-tests--frame-pixel-limit 32767
  "The largest width or height of a resize message that a session uses.")

(defun remoteterm-tests--random-frame-pixels ()
  "Return a random width or height of a resize message.
The value is not valid in one of four cases: 0, or more than
`remoteterm-tests--frame-pixel-limit'.  A valid value is often small,
below the minimum size of Emacs."
  (pcase (rd-gen-integer 0 3)
    (0 (rd-gen-element (list 0 (1+ remoteterm-tests--frame-pixel-limit)
                             #xffffffff)))
    (1 (rd-gen-integer 1 100))
    (_ (rd-gen-integer 1 2000))))

(defun remoteterm-tests--random-resizes ()
  "Return a list of random resize messages: (TARGET WIDTH HEIGHT).
TARGET is `frame', or `other' for a frame that does not exist."
  (cl-loop repeat (rd-gen-integer 1 6)
           collect (list (if (zerop (rd-gen-integer 0 3)) 'other 'frame)
                         (remoteterm-tests--random-frame-pixels)
                         (remoteterm-tests--random-frame-pixels))))

(defun remoteterm-tests--valid-resize-p (resize)
  "Return non-nil if RESIZE is for the frame and has a valid size."
  (pcase-let ((`(,target ,width ,height) resize))
    (and (eq target 'frame)
         (<= 1 width remoteterm-tests--frame-pixel-limit)
         (<= 1 height remoteterm-tests--frame-pixel-limit))))

(defun remoteterm-tests--send-resizes (client-side frame resizes)
  "Send RESIZES for FRAME from CLIENT-SIDE, and return the size of FRAME.
Wait until the session handled them, and do a redisplay.  The value
is (WIDTH . HEIGHT) in pixels."
  (let ((id (remoteterm-tests--frame-id frame)))
    (process-send-string
     client-side
     (concat (mapconcat
              (lambda (resize)
                (remote-display--encode
                 `(resize :frame ,(if (eq (car resize) 'frame) id
                                    (+ id 1000))
                          :width ,(nth 1 resize)
                          :height ,(nth 2 resize))))
              resizes)
             (remoteterm-tests--z-key frame))))
  (remoteterm-tests--read-events 1)
  (redraw-frame frame)
  (redisplay t)
  (cons (frame-pixel-width frame) (frame-pixel-height frame)))

(defun remoteterm-tests--run-resizes (resizes)
  "Send RESIZES to a session, and return the sizes of its frame.
The value is (BEFORE AFTER DEFINED MINIMUM).  BEFORE and AFTER are the
pixel sizes (WIDTH . HEIGHT) of the frame before the messages, and
after them and a redisplay.  DEFINED is the size of the last
define-frame message that the client got after it attached again.
MINIMUM is the size of the frame after a resize to 1 by 1 pixel."
  (remoteterm-tests--with-input-session
   (lambda (frame client-side &optional server-side)
     (let ((chunks nil)
           (before (cons (frame-pixel-width frame)
                         (frame-pixel-height frame))))
       (set-process-filter client-side
                           (lambda (_process string) (push string chunks)))
       (remote-display--detach "remote-input")
       (remote-display-attach "remote-input" server-side)
       (let ((after (remoteterm-tests--send-resizes client-side frame
                                                    resizes)))
         (accept-process-output nil 0.2)
         (let ((defined (cdar (last (seq-filter
                                     (lambda (message)
                                       (eq (car message) 'define-frame))
                                     (remoteterm-tests--all-messages
                                      (apply #'concat (reverse chunks))))))))
           (list before after
                 (cons (plist-get defined :width)
                       (plist-get defined :height))
                 (remoteterm-tests--send-resizes client-side frame
                                                 '((frame 1 1))))))))))

(ert-deftest remoteterm-tests-resize-tiny ()
  "A resize below the minimum size gives the windows their minimum size.
Thus the frame has a text column and a line.  This is the regression
test of a frame with no column, which made redisplay write past the
end of its mode line buffer."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (remoteterm-tests--daemon-call
                    'remoteterm-tests--resize-tiny-case)
                   '(t t t t)))))

(defun remoteterm-tests--resize-tiny-case ()
  "Resize a frame to 1 by 1 pixel.
Return a list of four values:
 - t if the root window has at least the minimum width of the windows
   of the frame, from `frame-windows-min-size'.
 - t if the root window and the minibuffer window have at least the
   minimum height of the windows of the frame.
 - t if the frame has a text column.
 - t if the frame has a text line."
  (remoteterm-tests--with-input-session
   (lambda (frame client-side &optional _server-side)
     (remoteterm-tests--send-resizes client-side frame '((frame 1 1)))
     (let ((root (frame-root-window frame)))
       (list (>= (window-pixel-width root)
                 (frame-windows-min-size frame t nil t))
             (>= (+ (window-pixel-height root)
                    (window-pixel-height (minibuffer-window frame)))
                 (frame-windows-min-size frame nil nil t))
             (> (frame-width frame) 0)
             (> (frame-height frame) 0))))))

(rd-daemon-deftest remoteterm-tests-resize-events
  "After resize messages, the frame has the size of the last valid
message for it, but not less than the minimum size of Emacs.  The
client gets a define-frame message with the size of the frame.  A
size of 0 or above the limit, and a message for a frame that does not
exist, change nothing."
  (let ((rd-gen-trials 20))
    (rd-gen-for-all
     #'remoteterm-tests--random-resizes
     (lambda (resizes)
       (pcase-let* ((`(,before ,after ,defined ,minimum)
                     (remoteterm-tests--run-resizes resizes))
                    (last (car (last (seq-filter
                                      #'remoteterm-tests--valid-resize-p
                                      resizes))))
                    (expected (if last (cons (nth 1 last) (nth 2 last))
                                before)))
         (should (equal defined after))
         (should (equal after
                        (cons (max (car expected) (car minimum))
                              (max (cdr expected) (cdr minimum))))))))))

(defun remoteterm-tests--resize-limit-case ()
  "Resize a frame to the largest width, and then above the limit.
Return the size of the frame after each step."
  (remoteterm-tests--with-input-session
   (lambda (frame client-side &optional _server-side)
     (let ((limit remoteterm-tests--frame-pixel-limit))
       (list (remoteterm-tests--send-resizes
              client-side frame `((frame ,limit 300)))
             (remoteterm-tests--send-resizes
              client-side frame `((frame 400 ,(1+ limit))))
             (remoteterm-tests--send-resizes
              client-side frame `((frame ,(1+ limit) 400))))))))

(ert-deftest remoteterm-tests-resize-limit ()
  "A resize message can give a frame the size of the limit, but no more."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (let ((limit remoteterm-tests--frame-pixel-limit))
      (should (equal (remoteterm-tests--daemon-call
                      'remoteterm-tests--resize-limit-case)
                     `((,limit . 300) (,limit . 300) (,limit . 300)))))))

;;;; Image types

(defconst remoteterm-tests--image-types
  '((xpm . "xpm") (png . "png") (jpeg . "jpeg") (svg . "rsvg")
    (gif . "gif") (tiff . "tiff") (webp . "webp") (pbm) (xbm))
  "The image types of a remote build.
The cdr of each entry is the name of the configure option of the
type, or nil if the type has no option.")

(defun remoteterm-tests--configured-without-p (option)
  "Return non-nil if the build was configured with --without-OPTION."
  (and option
       (string-match-p (concat "--without-" (regexp-quote option) "\\_>")
                       system-configuration-options)))

(ert-deftest remoteterm-tests-image-types ()
  "Each image type is available, unless the build was configured without it."
  (skip-unless (featurep 'remote-test))
  ;; The failure names all the types that are not available.
  (should-not (seq-remove (pcase-lambda (`(,type . ,option))
                            (or (remoteterm-tests--configured-without-p
                                 option)
                                (image-type-available-p type)))
                          remoteterm-tests--image-types)))

(defun remoteterm-tests--pbm (width height)
  "Return the data of a plain PBM image of WIDTH x HEIGHT pixels."
  (concat (format "P1\n%d %d\n" width height)
          (mapconcat (lambda (_) (make-string width ?0))
                     (make-list height nil) "\n")
          "\n"))

(defun remoteterm-tests--xpm (width height)
  "Return the data of an XPM image of WIDTH x HEIGHT pixels."
  (concat "/* XPM */\nstatic char *image[] = {\n"
          (format "\"%d %d 1 1\",\n\". c #000000\",\n" width height)
          (mapconcat (lambda (_) (format "\"%s\"" (make-string width ?.)))
                     (make-list height nil) ",\n")
          "};\n"))

(defun remoteterm-tests--built-in-image ()
  "Return a random (TYPE WIDTH HEIGHT) for an image that Emacs decodes.
Emacs has the decoder of each such TYPE in its own code."
  (list (rd-gen-element '(pbm xpm)) (rd-gen-integer 1 64)
        (rd-gen-integer 1 64)))

(ert-deftest remoteterm-tests-built-in-image-size ()
  "An image of a built-in type has its size in pixels on a remote frame."
  (skip-unless (featurep 'remote-test))
  (let ((rd-gen-trials 30))
    (remoteterm-tests--with-display "remote-test"
      (remoteterm-tests--with-frame frame "remote-test" nil
        (rd-gen-for-all
         #'remoteterm-tests--built-in-image
         (lambda (case)
           (pcase-let* ((`(,type ,width ,height) case)
                        (data (funcall (if (eq type 'pbm)
                                           #'remoteterm-tests--pbm
                                         #'remoteterm-tests--xpm)
                                       width height)))
             (should (equal (image-size (create-image data type t :scale 1)
                                        t frame)
                            (cons width height))))))))))

;;;; Image transfer

(defconst remoteterm-tests--png-signature
  (unibyte-string 137 80 78 71 13 10 26 10)
  "The first bytes of each PNG file.")

(defun remoteterm-tests--draw-images (images before after)
  "Draw IMAGES on a new remote frame, and return the value of AFTER.
IMAGES is a list of image objects.  Insert each image on its own line
of a new buffer of the frame.  BEFORE and AFTER are as for
`remoteterm-tests--draw-buffer'."
  (remoteterm-tests--draw-buffer
   (lambda ()
     (dolist (image images)
       (insert-image image)
       (insert "\n")))
   before after))

(defun remoteterm-tests--draw-buffer (fill before after)
  "Draw a new buffer on a new remote frame, and return the value of AFTER.
Call FILL in the buffer, with the frame selected.  Attach a recording
client, call BEFORE with the name of the session, and redisplay.  Then
call AFTER with the name of the session, the frame, and the bytes that
the session sent for the redisplay."
  (let* ((display "remote-image")
         (frame (progn (x-open-connection display)
                       (x-create-frame-with-faces
                        '((display . "remote-image") (width . 80)
                          (height . 40)))))
         (buffer (generate-new-buffer "images")))
    (unwind-protect
        (progn
          (set-window-buffer (frame-root-window frame) buffer)
          (select-frame frame)
          (remote-display--focus frame)
          (with-current-buffer buffer
            (funcall fill))
          (remote-display--record display)
          (funcall before display)
          (redisplay t)
          (funcall after display frame
                   (remote-display--take-output display)))
      (delete-frame frame)
      (kill-buffer buffer))))

(defun remoteterm-tests--redisplay-output (display)
  "Redisplay, and return the bytes that the session DISPLAY sent."
  (redisplay t)
  (remote-display--take-output display))

(defun remoteterm-tests--pbm-image (width height &optional scale)
  "Return a PBM image of WIDTH x HEIGHT white pixels, with SCALE or 1."
  (create-image (remoteterm-tests--pbm width height) 'pbm t
                :scale (or scale 1)))

(defun remoteterm-tests--noise-image (width height)
  "Return a PBM image of WIDTH x HEIGHT random pixels.
Random pixels do not compress well, so the PNG file is large."
  (create-image
   (concat (format "P1\n%d %d\n" width height)
           (apply #'string
                  (cl-loop repeat (* width height)
                           collect (if (zerop (rd-gen-random 2)) ?0 ?1))))
   'pbm t :scale 1))

(defun remoteterm-tests--image-files (bytes)
  "Return the PNG files of the image-data messages in BYTES.
The value is an alist of (ID . FILE), in the order of the first
image-data message of each ID.  FILE is as for
`remoteterm-tests--joined-image'."
  (let ((data (mapcar #'cdr
                      (seq-filter (lambda (message)
                                    (eq (car message) 'image-data))
                                  (remoteterm-tests--all-messages bytes)))))
    (mapcar (lambda (id)
              (cons id (remoteterm-tests--joined-image
                        (seq-filter (lambda (fields)
                                      (= (plist-get fields :image) id))
                                    data))))
            (seq-uniq (mapcar (lambda (fields) (plist-get fields :image))
                              data)))))

(defun remoteterm-tests--joined-image (data)
  "Return the file of DATA, the image-data fields of one image.
Each message must have the total of the first one, and must start
where the one before it ends.  If the bytes of DATA are the total
bytes, the value is a unibyte string with them.  If they are fewer,
the value is the symbol `partial'.  Else it is the symbol `broken'."
  (let ((total (plist-get (car data) :total))
        (offset 0))
    (cond
     ((not (cl-every (lambda (fields)
                       (prog1 (and (= (plist-get fields :total) total)
                                   (= (plist-get fields :offset) offset))
                         (setq offset (+ offset (length (plist-get fields
                                                                   :bytes))))))
                     data))
      'broken)
     ((< offset total) 'partial)
     ((> offset total) 'broken)
     (t (apply #'unibyte-string
               (mapcan (lambda (fields)
                         (append (plist-get fields :bytes) nil))
                       data))))))

(defun remoteterm-tests--png-size (file)
  "Return (WIDTH . HEIGHT) from the IHDR chunk of the PNG FILE.
Return nil if FILE is not a unibyte string that starts with the PNG
signature and the IHDR chunk."
  (and (stringp file)
       (>= (length file) 24)
       (string-prefix-p remoteterm-tests--png-signature file)
       (equal (substring file 12 16) "IHDR")
       (cons (remoteterm-tests--uint32 file 16)
             (remoteterm-tests--uint32 file 20))))

(defun remoteterm-tests--uint32 (bytes start)
  "Return the big-endian 32-bit number at START of the string BYTES."
  (seq-reduce (lambda (number byte) (+ (* number 256) byte))
              (substring bytes start (+ start 4)) 0))

(defun remoteterm-tests--image-positions (type id messages)
  "Return the indexes of the messages of TYPE with the image ID in MESSAGES."
  (cl-loop for message in messages
           for index from 0
           when (and (eq (car message) type)
                     (eql (plist-get (cdr message) :image) id))
           collect index))

(defun remoteterm-tests--drawn-after-data-p (id messages)
  "Return non-nil if MESSAGES draw the image ID after its last data."
  (let ((data (remoteterm-tests--image-positions 'image-data id messages)))
    (and data
         (seq-some (lambda (draw) (> draw (car (last data))))
                   (remoteterm-tests--image-positions 'draw-image id
                                                      messages)))))

(defun remoteterm-tests--image-size-case ()
  "Return a random (WIDTH HEIGHT) of a small image."
  (list (rd-gen-integer 1 64) (rd-gen-integer 1 64)))

(rd-daemon-deftest remoteterm-tests-image-data
  "The data of an image is a PNG file with its size (law 47)."
  (let ((rd-gen-trials 10))
    (rd-gen-for-all
     #'remoteterm-tests--image-size-case
     (lambda (case)
       (let* ((image (apply #'remoteterm-tests--pbm-image case))
              (result (remoteterm-tests--draw-images
                       (list image) #'ignore
                       (lambda (_display frame output)
                         (list (image-size image t frame)
                               (remoteterm-tests--image-files output))))))
         (pcase-let ((`(,size ,files) result))
           (should (= (length files) 1))
           (should (equal (remoteterm-tests--png-size (cdar files))
                          size))))))))

(rd-daemon-deftest remoteterm-tests-image-scale
  "An image with :scale 2 has a PNG file of two times the size (law 48)."
  (let ((rd-gen-trials 5))
    (rd-gen-for-all
     #'remoteterm-tests--image-size-case
     (lambda (case)
       (pcase-let* ((`(,width ,height) case)
                    (files (remoteterm-tests--draw-images
                            (list (remoteterm-tests--pbm-image width height)
                                  (remoteterm-tests--pbm-image width height 2))
                            #'ignore
                            (lambda (_display _frame output)
                              (remoteterm-tests--image-files output))))
                    (`(,one ,two) (mapcar (lambda (file)
                                            (remoteterm-tests--png-size
                                             (cdr file)))
                                          files)))
         (should (= (length files) 2))
         (should (equal two (cons (* 2 (car one)) (* 2 (cdr one))))))))))

(rd-daemon-deftest remoteterm-tests-image-before-draw
  "With room in the queue, image data comes before its draw (law 49)."
  (let ((rd-gen-trials 10))
    (rd-gen-for-all
     (lambda () (cons (rd-gen-integer 0 65535)
                      (remoteterm-tests--image-size-case)))
     (lambda (case)
       (pcase-let* ((`(,queued . ,size) case)
                    (output (remoteterm-tests--draw-images
                             (list (apply #'remoteterm-tests--pbm-image size))
                             (lambda (display)
                               (remote-display--set-queued display queued))
                             (lambda (_display _frame output) output)))
                    (messages (remoteterm-tests--all-messages output))
                    (files (remoteterm-tests--image-files output))
                    (id (caar files)))
         (should (= (length files) 1))
         (should (stringp (cdar files)))
         (should (> (car (remoteterm-tests--image-positions
                          'draw-image id messages))
                    (car (last (remoteterm-tests--image-positions
                                'image-data id messages))))))))))

(ert-deftest remoteterm-tests-image-flush ()
  "The image data of an update goes out with its draws in one flush (law 50)."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let ((`(,flushes ,files)
                 (remoteterm-tests--daemon-call
                  'remoteterm-tests--image-flush-case)))
      (should (= (length files) 1))
      (should (stringp (cdar files)))
      (should (= flushes 1)))))

(defun remoteterm-tests--image-flush-case ()
  "Draw an image, and return (FLUSHES FILES).
FLUSHES is the number of flushes of the redisplay that sent bytes, and
FILES is as for `remoteterm-tests--image-files'."
  (let ((before 0))
    (remoteterm-tests--draw-images
     (list (remoteterm-tests--pbm-image 40 30))
     (lambda (display) (setq before (remote-display--flushes display)))
     (lambda (display _frame output)
       (list (- (remote-display--flushes display) before)
             (remoteterm-tests--image-files output))))))

(defun remoteterm-tests--image-runs (messages)
  "Return the image IDs of the image-data messages in MESSAGES, as runs.
Each run of image-data messages with one ID gives one element."
  (let ((runs nil))
    (dolist (id (remoteterm-tests--image-ids-in-order 'image-data messages))
      (unless (eql id (car runs))
        (push id runs)))
    (nreverse runs)))

(defun remoteterm-tests--image-ids-in-order (type messages)
  "Return the image ID of each message of TYPE in MESSAGES, in order."
  (mapcar (lambda (fields) (plist-get fields :image))
          (remoteterm-tests--of-messages type messages)))

(defun remoteterm-tests--should-send-images (count bytes)
  "Check the image laws for BYTES, the bytes of COUNT images, with `should'.
BYTES has one complete PNG file for each of the COUNT IDs, the data of
two IDs do not interleave, and each ID has a draw after its last data."
  (let ((files (remoteterm-tests--image-files bytes))
        (messages (remoteterm-tests--all-messages bytes)))
    (should (= (length files) count))
    (should (cl-every #'stringp (mapcar #'cdr files)))
    (should (= (length (remoteterm-tests--image-runs messages)) count))
    (dolist (file files)
      (should (remoteterm-tests--drawn-after-data-p (car file) messages)))))

(defun remoteterm-tests--pbm-images (sizes)
  "Return a PBM image for each (WIDTH HEIGHT) in SIZES."
  (mapcar (lambda (size) (apply #'remoteterm-tests--pbm-image size))
          sizes))

(defun remoteterm-tests--few-image-sizes ()
  "Return a list of from 1 to 3 random (WIDTH HEIGHT) of small images."
  (cl-loop repeat (rd-gen-integer 1 3)
           collect (remoteterm-tests--image-size-case)))

(rd-daemon-deftest remoteterm-tests-image-once
  "A client gets the data of an image once for each attach (law 51)."
  (let ((rd-gen-trials 3))
    (rd-gen-for-all
     #'remoteterm-tests--few-image-sizes
     (lambda (sizes)
       (pcase-let* ((`(,first ,again ,attach)
                     (remoteterm-tests--image-once-output sizes))
                    (files (remoteterm-tests--image-files first)))
         (remoteterm-tests--should-send-images (length sizes) first)
         (should-not (remoteterm-tests--image-files again))
         (should (equal (seq-uniq (remoteterm-tests--image-ids-in-order
                                   'draw-image
                                   (remoteterm-tests--all-messages again)))
                        (mapcar #'car files)))
         (should (equal (remoteterm-tests--image-files attach) files)))))))

(defun remoteterm-tests--image-once-output (sizes)
  "Draw images of SIZES three times, and return the bytes of each draw.
SIZES is as for `remoteterm-tests--pbm-images'.  The first draw is
the first one, the second one is after `redraw-frame', and the third
one is after a new attach."
  (remoteterm-tests--draw-images
   (remoteterm-tests--pbm-images sizes)
   #'ignore
   (lambda (display frame output)
     (list output
           (progn (redraw-frame frame)
                  (remoteterm-tests--redisplay-output display))
           (progn (remote-display--detach display)
                  (remote-display--record display)
                  (remoteterm-tests--redisplay-output display))))))

(rd-daemon-deftest remoteterm-tests-image-pace
  "Image data waits while the sink of the session is full (law 52)."
  (let ((rd-gen-trials 3))
    (rd-gen-for-all
     (lambda () (list (remoteterm-tests--few-image-sizes)
                      (rd-gen-integer 65536 200000)
                      (rd-gen-integer 0 65535)))
     (lambda (case)
       (pcase-let ((`(,full ,drained ,redrawn)
                    (apply #'remoteterm-tests--image-pace-output case)))
         (should-not (remoteterm-tests--image-files full))
         (remoteterm-tests--should-send-images
          (length (car case)) (concat full drained redrawn)))))))

(defun remoteterm-tests--image-pace-output (sizes full drained)
  "Draw images of SIZES with a full sink, and return the bytes of three steps.
SIZES is as for `remoteterm-tests--pbm-images'.  The sink has FULL
bytes that it did not send in the first step, the draw.  The second
step is an empty queue of the transport, after which the sink has
DRAINED bytes.  The third step is the redisplay after it."
  (remoteterm-tests--draw-images
   (remoteterm-tests--pbm-images sizes)
   (lambda (display) (remote-display--set-queued display full))
   (lambda (display _frame output)
     (list output
           (progn (remote-display--set-queued display drained)
                  (remote-display--queue-empty display)
                  (remote-display--take-output display))
           (remoteterm-tests--redisplay-output display)))))

(rd-daemon-deftest remoteterm-tests-image-queue
  "The data of images go out one image after the other (law 53)."
  (let ((rd-gen-trials 2))
    (rd-gen-for-all
     (lambda () (cl-loop repeat (rd-gen-integer 2 3)
                         collect (list (rd-gen-integer 300 320)
                                       (rd-gen-integer 200 220))))
     (lambda (sizes)
       (pcase-let* ((`(,update ,rest)
                     (remoteterm-tests--image-queue-output sizes))
                    (first (caar (remoteterm-tests--image-files update))))
         ;; The queue lets the update send only a part of the first image.
         (should (eq (cdr (assq first (remoteterm-tests--image-files update)))
                     'partial))
         (remoteterm-tests--should-send-images (length sizes)
                                               (concat update rest)))))))

(defun remoteterm-tests--image-queue-output (sizes)
  "Draw large images with a queue that has room for one message.
SIZES is a list of (WIDTH HEIGHT) of images with random pixels.
Return (UPDATE REST).  UPDATE is the bytes of the update.  REST is
the bytes after an empty queue of the transport, and of the redisplay
after it."
  (remoteterm-tests--draw-images
   (mapcar (lambda (size) (apply #'remoteterm-tests--noise-image size))
           sizes)
   (lambda (display) (remote-display--set-queued display 65535 t))
   (lambda (display _frame output)
     (list output
           (progn (remote-display--set-queued display 0 t)
                  (remote-display--queue-empty display)
                  (concat (remote-display--take-output display)
                          (remoteterm-tests--redisplay-output display)))))))

(ert-deftest remoteterm-tests-image-limit ()
  "An image with more than 8192 pixels on a side is not sent (law 54)."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let ((`(,wide ,tall ,largest)
                 (remoteterm-tests--daemon-call
                  'remoteterm-tests--image-limit-case)))
      (dolist (large (list wide tall))
        (should-not (seq-some (lambda (message)
                                (memq (car message) '(image-data draw-image)))
                              (remoteterm-tests--all-messages large))))
      (should (equal (mapcar (lambda (file)
                               (remoteterm-tests--png-size (cdr file)))
                             (remoteterm-tests--image-files largest))
                     '((8192 . 1)))))))

(defun remoteterm-tests--image-limit-case ()
  "Return the bytes of a draw of images of 8193 x 1, 1 x 8193 and 8192 x 1."
  (let ((max-image-size 100000))
    (mapcar (lambda (size)
              (remoteterm-tests--draw-images
               (list (apply #'remoteterm-tests--pbm-image size))
               #'ignore
               (lambda (_display _frame output) output)))
            '((8193 1) (1 8193) (8192 1)))))

(ert-deftest remoteterm-tests-image-size-limit ()
  "An image with a PNG file above the size limit is an outline (law 63).
The session remembers the refusal until the pixels of the image change."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let ((`(,outline . ,draws)
                 (remoteterm-tests--daemon-call
                  'remoteterm-tests--image-size-limit-case)))
      (dolist (messages draws)
        (should-not (seq-some (lambda (message)
                                (memq (car message) '(image-data draw-image)))
                              messages))
        (should (cl-subsetp outline
                            (mapcar (lambda (fields)
                                      (map-delete (map-delete
                                                   (copy-sequence fields)
                                                   :frame)
                                                  :color))
                                    (remoteterm-tests--of-messages
                                     'clear-area messages))
                            :test #'equal))))))

(defun remoteterm-tests--image-size-limit-case ()
  "Draw an image with a low limit of the PNG size, then with the default.
Return (OUTLINE REFUSED AGAIN).  OUTLINE is the lines of the outline
of the image, as for `remoteterm-tests--image-outline'.  REFUSED is
the messages of the draw with the low limit.  AGAIN is the messages of
a redraw of the frame with the default limit."
  (let ((default (remote-display--set-max-image-size 100)))
    (unwind-protect
        (remoteterm-tests--draw-images
         (list (remoteterm-tests--noise-image 40 30))
         #'ignore
         (lambda (display frame output)
           (let ((edges (window-inside-pixel-edges (frame-root-window frame))))
             (list (remoteterm-tests--image-outline (nth 0 edges) (nth 1 edges)
                                                    40 30)
                   (remoteterm-tests--messages output)
                   (progn (remote-display--set-max-image-size default)
                          (redraw-frame frame)
                          (remoteterm-tests--messages
                           (remoteterm-tests--redisplay-output display)))))))
      (remote-display--set-max-image-size default))))

(ert-deftest remoteterm-tests-image-id-wrap ()
  "An image ID is not 0 after the counter of IDs wraps (law 64)."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let ((`(,files ,draws)
                 (remoteterm-tests--daemon-call
                  'remoteterm-tests--image-id-wrap-case)))
      (should (= (length files) 1))
      (should (stringp (cdar files)))
      (should-not (eql (caar files) 0))
      (should (equal draws (list (caar files)))))))

(defun remoteterm-tests--image-id-wrap-case ()
  "Draw an image after the last image ID, and return (FILES DRAWS).
FILES is as for `remoteterm-tests--image-files', and DRAWS is the
IDs of the draw-image messages, with no copy."
  (remote-display--set-last-image-id #xffffffff)
  (remoteterm-tests--draw-images
   (list (remoteterm-tests--pbm-image 10 10))
   #'ignore
   (lambda (_display _frame output)
     (list (remoteterm-tests--image-files output)
           (remoteterm-tests--image-ids
            '(draw-image) (remoteterm-tests--all-messages output))))))

;;;; Image pixels

(defun remoteterm-tests--ppm-case ()
  "Return a random (WIDTH HEIGHT SCALE PIXELS) of a small PPM image.
PIXELS has the color of each pixel, row after row, as a list of three
8-bit components."
  (let ((width (rd-gen-integer 1 64))
        (height (rd-gen-integer 1 64)))
    (list width height (rd-gen-element '(1 2))
          (cl-loop repeat (* width height)
                   collect (remoteterm-tests--rgb)))))

(defun remoteterm-tests--ppm (width height pixels)
  "Return the data of a plain PPM image of WIDTH x HEIGHT PIXELS.
PIXELS is as for `remoteterm-tests--ppm-case'."
  (concat (format "P3\n%d %d\n255\n" width height)
          (mapconcat (lambda (rgb) (mapconcat #'number-to-string rgb " "))
                     pixels "\n")
          "\n"))

(defun remoteterm-tests--scaled-pixels (width height scale pixels)
  "Return the ARGB32 pixels of the PPM image of the arguments at SCALE.
WIDTH, HEIGHT and PIXELS are as for `remoteterm-tests--ppm-case'.
Each pixel is opaque, and a scale makes SCALE x SCALE copies of it."
  (let ((source (vconcat pixels)))
    (vconcat
     (cl-loop for y below (* scale height)
              nconc (cl-loop for x below (* scale width)
                             collect (logior
                                      #xff000000
                                      (remoteterm-tests--rgb-pixel
                                       (aref source
                                             (+ (* (/ y scale) width)
                                                (/ x scale))))))))))

(defun remoteterm-tests--sent-pixels (image)
  "Draw IMAGE, and return the decoded pixels of the PNG file that went out.
The value is as for `remote-display--png-pixels', or nil if no file,
or more than one file, went out."
  (let ((files (remoteterm-tests--draw-images
                (list image) #'ignore
                (lambda (_display _frame output)
                  (remoteterm-tests--image-files output)))))
    (and (= (length files) 1)
         (remote-display--png-pixels (cdar files)))))

(rd-daemon-deftest remoteterm-tests-image-pixels
  "The PNG file of an image has the pixels of the image at its scale (law 65)."
  (let ((rd-gen-trials 10))
    (rd-gen-for-all
     #'remoteterm-tests--ppm-case
     (lambda (case)
       (pcase-let* ((`(,width ,height ,scale ,pixels) case)
                    (image (create-image (remoteterm-tests--ppm width height
                                                                pixels)
                                         'pbm t :scale scale
                                         :transform-smoothing nil)))
         (should (equal (remoteterm-tests--sent-pixels image)
                        (cons (* scale width)
                              (cons (* scale height)
                                    (remoteterm-tests--scaled-pixels
                                     width height scale pixels))))))))))

(defun remoteterm-tests--masked-case ()
  "Return a random (WIDTH HEIGHT CLEAR) of a small XPM image.
CLEAR has a boolean for each pixel, row after row: t if the pixel has
no color."
  (let ((width (rd-gen-integer 1 32))
        (height (rd-gen-integer 1 32)))
    (list width height
          (cl-loop repeat (* width height)
                   collect (zerop (rd-gen-random 2))))))

(defun remoteterm-tests--masked-xpm (width height clear)
  "Return the data of an XPM image of WIDTH x HEIGHT pixels.
CLEAR is as for `remoteterm-tests--masked-case'.  A pixel with no
color is None, and the other pixels are red."
  (concat "/* XPM */\nstatic char *image[] = {\n"
          (format "\"%d %d 2 1\",\n\". c None\",\n\"# c #ff0000\",\n"
                  width height)
          (mapconcat (lambda (row)
                       (format "\"%s\""
                               (mapconcat (lambda (none) (if none "." "#"))
                                          row "")))
                     (seq-partition clear width) ",\n")
          "};\n"))

(rd-daemon-deftest remoteterm-tests-image-alpha
  "The PNG file of an image keeps the pixels with no color clear (law 66)."
  (let ((rd-gen-trials 10))
    (rd-gen-for-all
     #'remoteterm-tests--masked-case
     (lambda (case)
       (pcase-let* ((`(,width ,height ,clear) case)
                    (`(,_width ,_height . ,pixels)
                     (remoteterm-tests--sent-pixels
                      (create-image (remoteterm-tests--masked-xpm width height
                                                                  clear)
                                    'xpm t :scale 1))))
         (should (equal (mapcar (lambda (pixel) (ash pixel -24)) pixels)
                        (mapcar (lambda (none) (if none 0 #xff)) clear))))))))

;;;; Image draw

(defun remoteterm-tests--image-layout-case ()
  "Return a random (WIDTH HEIGHT SCALE MARGIN ASCENT COLUMNS ROWS BACKGROUND).
WIDTH and HEIGHT are the size of a PBM image, and SCALE, MARGIN and
ASCENT are its properties.  COLUMNS and ROWS cut the scaled image into
slices, as for `rd-gen-parts'.  BACKGROUND is the background of the
face of the slices, a list of three 8-bit components."
  (let ((width (rd-gen-integer 1 40))
        (height (rd-gen-integer 1 40))
        (scale (rd-gen-element '(1 2))))
    (list width height scale (rd-gen-integer 0 5)
          (rd-gen-element (list 'center (rd-gen-integer 0 100)))
          (rd-gen-parts (* scale width) 3)
          (rd-gen-parts (* scale height) 3)
          (remoteterm-tests--rgb))))

(defun remoteterm-tests--insert-slices (image columns rows face)
  "Insert the slices of IMAGE, cut by COLUMNS and ROWS, with FACE.
Each row of slices is on its own line.  COLUMNS and ROWS are as for
`rd-gen-parts'.  Put the point of the window after the slices."
  (dolist (row rows)
    (dolist (column columns)
      (insert (propertize " " 'face face
                          'display `((slice ,(car column) ,(car row)
                                            ,(cdr column) ,(cdr row))
                                     ,image))))
    (insert (propertize "\n" 'line-height t)))
  (set-window-point (selected-window) (point-max)))

(defun remoteterm-tests--image-clip (fields)
  "Return the clip rectangle of the draw-image FIELDS, as clear-area fields."
  (list :x (plist-get fields :clip-x) :y (plist-get fields :clip-y)
        :width (plist-get fields :clip-width)
        :height (plist-get fields :clip-height)))

(defun remoteterm-tests--rectangle-inside-p (a b)
  "Return non-nil if each pixel of the rectangle A is inside the rectangle B.
A and B are fields with :x, :y, :width and :height."
  (and (<= (plist-get b :x) (plist-get a :x))
       (<= (plist-get b :y) (plist-get a :y))
       (<= (+ (plist-get a :x) (plist-get a :width))
           (+ (plist-get b :x) (plist-get b :width)))
       (<= (+ (plist-get a :y) (plist-get a :height))
           (+ (plist-get b :y) (plist-get b :height)))))

(defun remoteterm-tests--area (rectangle)
  "Return the number of pixels of RECTANGLE.
RECTANGLE is as for `remoteterm-tests--rectangle-inside-p'."
  (* (plist-get rectangle :width) (plist-get rectangle :height)))

(defun remoteterm-tests--image-draws (messages)
  "Return (PREVIOUS . FIELDS) for each draw-image message in MESSAGES.
FIELDS is the fields of the draw, and PREVIOUS is the message before
it.  An image-data message is not a draw, so PREVIOUS is not one."
  (cl-loop for (previous message)
           on (cons nil (seq-remove (lambda (message)
                                      (eq (car message) 'image-data))
                                    messages))
           when (eq (car message) 'draw-image)
           collect (cons previous (cdr message))))

(defun remoteterm-tests--image-layout-output (case)
  "Draw the image of CASE, and return a plist of the result.
CASE is as for `remoteterm-tests--image-layout-case'.  The plist has
the drawing messages in :messages, the left edge of the text area in
:left, and the scaled size of the image in :size, with no margin."
  (pcase-let* ((`(,width ,height ,scale ,margin ,ascent ,columns ,rows
                         ,background)
                case)
               (image (create-image (remoteterm-tests--pbm width height)
                                    'pbm t :scale scale :margin margin
                                    :ascent ascent)))
    (remoteterm-tests--draw-buffer
     (lambda ()
       (remoteterm-tests--insert-slices
        image columns rows
        `(:background ,(remoteterm-tests--rgb-string background))))
     #'ignore
     (lambda (_display frame output)
       (list :messages (remoteterm-tests--messages output)
             :left (car (window-inside-pixel-edges (frame-root-window frame)))
             :size (let ((size (image-size image t frame)))
                     (cons (- (car size) (* 2 margin))
                           (- (cdr size) (* 2 margin)))))))))

(rd-daemon-deftest remoteterm-tests-image-position
  "Each slice of an image is drawn in its place on its background (law 55)."
  (let ((rd-gen-trials 15))
    (rd-gen-for-all
     #'remoteterm-tests--image-layout-case
     (lambda (case)
       (pcase-let* (((map :messages :left :size)
                     (remoteterm-tests--image-layout-output case))
                    (margin (nth 3 case))
                    (background (remoteterm-tests--rgb-pixel (nth 7 case)))
                    (draws (remoteterm-tests--image-draws messages))
                    (clips (seq-uniq (mapcar (lambda (draw)
                                               (remoteterm-tests--image-clip
                                                (cdr draw)))
                                             draws))))
         (should draws)
         (pcase-dolist (`(,previous . ,fields) draws)
           (let ((clip (remoteterm-tests--image-clip fields)))
             (should (eq (car previous) 'clear-area))
             (should (= (plist-get (cdr previous) :color) background))
             (should (remoteterm-tests--rectangle-inside-p clip
                                                           (cdr previous)))
             (should (remoteterm-tests--rectangle-inside-p
                      clip (list :x (plist-get fields :x)
                                 :y (plist-get fields :y)
                                 :width (car size) :height (cdr size))))
             (should (= (plist-get fields :x) (+ left margin)))))
         (should (= (apply #'+ (mapcar #'remoteterm-tests--area clips))
                    (* (car size) (cdr size))))
         (should-not (cl-loop for (clip . others) on clips
                              thereis (seq-some
                                       (lambda (other)
                                         (remoteterm-tests--overlap-p
                                          clip other))
                                       others))))))))

(ert-deftest remoteterm-tests-image-mode-line ()
  "The clip of an image in the mode line is inside the mode line (law 56)."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let ((`(,draws ,mode-line)
                 (remoteterm-tests--daemon-call
                  'remoteterm-tests--image-mode-line-case)))
      (should draws)
      (dolist (draw draws)
        (should (remoteterm-tests--rectangle-inside-p
                 (remoteterm-tests--image-clip (cdr draw)) mode-line))))))

(defun remoteterm-tests--image-mode-line-case ()
  "Draw an image with a margin in the mode line, and return (DRAWS MODE-LINE).
DRAWS is as for `remoteterm-tests--image-draws', and MODE-LINE is the
rectangle of the mode line, as clear-area fields."
  (let ((image (create-image (remoteterm-tests--pbm 10 10) 'pbm t
                             :scale 1 :margin 3)))
    (remoteterm-tests--draw-buffer
     (lambda ()
       (setq mode-line-format (list (propertize "x" 'display image))))
     #'ignore
     (lambda (_display frame output)
       (let* ((window (frame-root-window frame))
              (edges (window-pixel-edges window))
              (height (window-mode-line-height window)))
         (list (remoteterm-tests--image-draws
                (remoteterm-tests--messages output))
               (list :x (nth 0 edges) :y (- (nth 3 edges) height)
                     :width (- (nth 2 edges) (nth 0 edges))
                     :height height)))))))

(defun remoteterm-tests--image-outline (x y width height)
  "Return the four lines at the edges of a rectangle, as clear-area fields.
The rectangle is at X, Y, and has the size WIDTH x HEIGHT."
  (mapcar (lambda (line)
            (cl-mapcan #'list '(:x :y :width :height) line))
          (list (list x y width 1)
                (list x (+ y height -1) width 1)
                (list x y 1 height)
                (list (+ x width -1) y 1 height))))

(defun remoteterm-tests--rectangle< (a b)
  "Return non-nil if the rectangle A comes before the rectangle B.
Compare :x, then :y, then :width, then :height."
  (let ((keys '(:x :y :width :height)))
    (version-list-< (mapcar (lambda (key) (plist-get a key)) keys)
                    (mapcar (lambda (key) (plist-get b key)) keys))))

(defun remoteterm-tests--load-failure-output (foreground)
  "Draw an image that cannot load with FOREGROUND, and return a plist.
FOREGROUND is a list of three 8-bit components.  The plist has the
drawing messages in :messages, and the left edge of the text area in
:left."
  (remoteterm-tests--draw-buffer
   (lambda ()
     (remoteterm-tests--insert-slices
      (create-image "not an image" 'pbm t) '((0 . 30)) '((0 . 30))
      `(:foreground ,(remoteterm-tests--rgb-string foreground)
        :background "#ffffff")))
   #'ignore
   (lambda (_display frame output)
     (list :messages (remoteterm-tests--messages output)
           :left (car (window-inside-pixel-edges
                       (frame-root-window frame)))))))

(rd-daemon-deftest remoteterm-tests-image-load-failure
  "An image that cannot load is drawn as an outline (law 57)."
  (let ((rd-gen-trials 10))
    (rd-gen-for-all
     ;; Not black or white: the frame has these colors.
     (lambda () (list (rd-gen-integer 1 254) (rd-gen-integer 0 255)
                      (rd-gen-integer 0 255)))
     (lambda (foreground)
       (pcase-let* ((color (remoteterm-tests--rgb-pixel foreground))
                    ((map :messages :left)
                     (remoteterm-tests--load-failure-output foreground))
                    (lines (seq-uniq
                            (mapcar (lambda (fields)
                                      (map-delete (copy-sequence fields)
                                                  :frame))
                                    (seq-filter
                                     (lambda (fields)
                                       (= (plist-get fields :color) color))
                                     (remoteterm-tests--of-messages
                                      'clear-area messages)))))
                    ;; A failed image is 30 x 30 pixels (image.c).
                    (top (if lines
                             (apply #'min (mapcar (lambda (fields)
                                                    (plist-get fields :y))
                                                  lines))
                           0)))
         (should-not (assq 'draw-image messages))
         (should (equal (sort (mapcar (lambda (fields)
                                        (map-delete fields :color))
                                      lines)
                              #'remoteterm-tests--rectangle<)
                        (sort (remoteterm-tests--image-outline left top 30 30)
                              #'remoteterm-tests--rectangle<))))))))

(defun remoteterm-tests--of-messages (type messages)
  "Return the fields of the messages of TYPE in the list MESSAGES."
  (mapcar #'cdr (seq-filter (lambda (message) (eq (car message) type))
                            messages)))

(defun remoteterm-tests--image-cursor-output (mask)
  "Draw a box cursor on an image, and return the drawing messages.
The image has a mask if MASK is non-nil."
  (let ((image (create-image "P1\n4 4\n1000\n0100\n0010\n0001\n"
                             'pbm t :scale 1 :mask (and mask 'heuristic))))
    (remoteterm-tests--draw-buffer
     (lambda ()
       (insert-image image)
       (setq cursor-type 'box)
       (set-window-point (selected-window) 1))
     #'ignore
     (lambda (_display _frame output)
       (remoteterm-tests--messages output)))))

(ert-deftest remoteterm-tests-image-cursor ()
  "A box cursor on an image keeps the box (law 58)."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let* ((`(,masked ,opaque)
                  (remoteterm-tests--daemon-call
                   'remoteterm-tests--image-cursor-case))
                 (cursor (cdr (assq 'draw-cursor (reverse masked))))
                 (after (cdr (memq (assq 'draw-cursor (reverse masked))
                                   masked))))
      (should (= (plist-get cursor :kind) 0))
      (should (assq 'draw-image after))
      (should-not (seq-some (lambda (message)
                              (and (eq (car message) 'clear-area)
                                   (remoteterm-tests--overlap-p (cdr message)
                                                                cursor)))
                            after))
      (should (= (plist-get (cdr (assq 'draw-cursor (reverse opaque))) :kind)
                 1)))))

(defun remoteterm-tests--image-cursor-case ()
  "Return the drawing messages of a box cursor on two images.
The first image has a mask, and the second one has none."
  (list (remoteterm-tests--image-cursor-output t)
        (remoteterm-tests--image-cursor-output nil)))

;;;; Image forget

(defun remoteterm-tests--image-ids (types messages)
  "Return the image IDs of the messages of TYPES in MESSAGES, with no copy."
  (seq-uniq (mapcar (lambda (message) (plist-get (cdr message) :image))
                    (seq-filter (lambda (message) (memq (car message) types))
                                messages))))

(defun remoteterm-tests--image-sizes-case ()
  "Return a list of from 1 to 4 random (WIDTH HEIGHT) of small images."
  (cl-loop repeat (rd-gen-integer 1 4)
           collect (remoteterm-tests--image-size-case)))

(rd-daemon-deftest remoteterm-tests-image-forget
  "The session forgets each image of a cleared cache (law 59)."
  (let ((rd-gen-trials 5))
    (rd-gen-for-all
     #'remoteterm-tests--image-sizes-case
     (lambda (sizes)
       (pcase-let* ((`(,first ,cleared ,later)
                     (remoteterm-tests--draw-images
                      (mapcar (lambda (size)
                                (apply #'remoteterm-tests--pbm-image size))
                              sizes)
                      #'ignore
                      (lambda (display frame output)
                        (list output
                              (progn (clear-image-cache t)
                                     (remoteterm-tests--redisplay-output
                                      display))
                              (progn (redraw-frame frame)
                                     (remoteterm-tests--redisplay-output
                                      display))))))
                    (had (mapcar #'car (remoteterm-tests--image-files first)))
                    (after (remoteterm-tests--all-messages
                            (concat cleared later))))
         (should had)
         (should (cl-subsetp had (remoteterm-tests--image-ids
                                  '(forget-image) after)))
         (should-not (cl-intersection
                      had (remoteterm-tests--image-ids '(draw-image)
                                                       after))))))))

(defun remoteterm-tests--image-op ()
  "Return a random step for the law of image IDs that go.
The step is (insert WIDTH HEIGHT), (flush INDEX), (clear) or
\(redisplay)."
  (pcase (rd-gen-element '(insert flush clear redisplay))
    ('insert (cons 'insert (remoteterm-tests--image-size-case)))
    ('flush (list 'flush (rd-gen-random 8)))
    (op (list op))))

(defun remoteterm-tests--image-ops-case ()
  "Return a list of from 1 to 12 random steps of `remoteterm-tests--image-op'."
  (cl-loop repeat (rd-gen-integer 1 12)
           collect (remoteterm-tests--image-op)))

(defun remoteterm-tests--image-ops-output (ops)
  "Draw an image, do OPS, and return all the bytes that the session sent.
OPS is as for `remoteterm-tests--image-ops-case'.  A redisplay comes
after the last step."
  (let ((images (list (remoteterm-tests--pbm-image 10 10))))
    (remoteterm-tests--draw-images
     images #'ignore
     (lambda (display frame output)
       (with-current-buffer (window-buffer (frame-root-window frame))
         (dolist (op ops)
           (pcase op
             (`(insert ,width ,height)
              (let ((image (remoteterm-tests--pbm-image width height)))
                (push image images)
                (goto-char (point-max))
                (insert-image image)))
             (`(flush ,index)
              (image-flush (nth (% index (length images)) images) frame))
             ('(clear) (clear-image-cache t))
             ('(redisplay) (redisplay t)))
           (setq output (concat output
                                (remote-display--take-output display)))))
       (concat output (remoteterm-tests--redisplay-output display))))))

(defun remoteterm-tests--used-after-forget-p (messages)
  "Return non-nil if an image message in MESSAGES uses a forgotten ID.
That is an image-data or draw-image message that comes after a
forget-image message with its ID."
  (let ((forgotten nil))
    (seq-some (lambda (message)
                (let ((id (plist-get (cdr message) :image)))
                  (pcase (car message)
                    ('forget-image (push id forgotten) nil)
                    ((or 'image-data 'draw-image) (memq id forgotten)))))
              messages)))

(rd-daemon-deftest remoteterm-tests-image-id-not-used-again
  "No image message uses an ID after its forget-image message (law 60)."
  (let ((rd-gen-trials 10))
    (rd-gen-for-all
     #'remoteterm-tests--image-ops-case
     (lambda (ops)
       (should-not (remoteterm-tests--used-after-forget-p
                    (remoteterm-tests--all-messages
                     (remoteterm-tests--image-ops-output ops))))))))

(defun remoteterm-tests--canvas-steps (refreshes)
  "Draw a canvas, refresh it REFRESHES times, and return the bytes of each step.
The first step is the first draw.  Each other step is a refresh of
the canvas with new pixels, and a redisplay."
  (let* ((pixels (make-vector (* 8 8) #xff000000))
         (canvas (create-image pixels 'canvas t
                               :data-width 8 :data-height 8)))
    (remoteterm-tests--draw-buffer
     (lambda () (insert (propertize "#" 'display canvas)))
     #'ignore
     (lambda (display _frame output)
       (cons output
             (cl-loop for color from 1 to refreshes
                      collect (progn
                                (fillarray pixels (logior #xff000000 color))
                                (canvas-refresh canvas 'reload-data)
                                (remoteterm-tests--redisplay-output
                                 display))))))))

(defun remoteterm-tests--live-image-ids (messages)
  "Return the IDs that the client has after MESSAGES.
That is each ID of an image-data message with no forget-image message."
  (cl-set-difference
   (remoteterm-tests--image-ids '(image-data) messages)
   (remoteterm-tests--image-ids '(forget-image) messages)))

(defun remoteterm-tests--last-drawn-id (messages)
  "Return the image ID of the last draw-image message in MESSAGES."
  (car (last (remoteterm-tests--image-ids '(draw-image) messages))))

(rd-daemon-deftest remoteterm-tests-canvas-refresh
  "A refresh of a canvas gives it a new ID and forgets the old one (law 61)."
  (let ((rd-gen-trials 5))
    (rd-gen-for-all
     (lambda () (rd-gen-integer 1 20))
     (lambda (refreshes)
       (let* ((steps (remoteterm-tests--canvas-steps refreshes))
              (sent (car steps))
              (drawn (remoteterm-tests--last-drawn-id
                      (remoteterm-tests--all-messages sent))))
         (dolist (step (cdr steps))
           (setq sent (concat sent step))
           (let* ((messages (remoteterm-tests--all-messages sent))
                  (id (remoteterm-tests--last-drawn-id
                       (remoteterm-tests--all-messages step))))
             (should id)
             (should-not (eql id drawn))
             (should (stringp (cdr (assq id (remoteterm-tests--image-files
                                             sent)))))
             (should (equal (remoteterm-tests--live-image-ids messages)
                            (list id)))
             (setq drawn id))))))))

(ert-deftest remoteterm-tests-image-forget-no-client ()
  "An image that goes with no client sends no forget-image (law 62)."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (pcase-let ((`(,output ,state)
                 (remoteterm-tests--daemon-call
                  'remoteterm-tests--image-forget-no-client-case)))
      (should (remoteterm-tests--image-files output))
      (should-not (assq 'forget-image
                        (remoteterm-tests--all-messages output)))
      (should (eq state 'alive)))))

(defun remoteterm-tests--image-forget-no-client-case ()
  "Free images with no client, and return (OUTPUT STATE).
OUTPUT is the bytes of the redisplay for a new client after a free of
the images while no client was attached.  STATE is `alive' after a
free of the images of a deleted session."
  (list (remoteterm-tests--draw-images
         (list (remoteterm-tests--pbm-image 20 10))
         #'ignore
         (lambda (display _frame _output)
           (remote-display--detach display)
           (clear-image-cache t)
           (remote-display--record display)
           (remoteterm-tests--redisplay-output display)))
        (remoteterm-tests--free-after-delete)))

(defun remoteterm-tests--free-after-delete ()
  "Draw an image, delete its session, free the images, and return `alive'."
  (let* ((display "remote-image-gone")
         (frame (progn (x-open-connection display)
                       (x-create-frame-with-faces
                        '((display . "remote-image-gone"))))))
    (with-selected-frame frame
      (with-temp-buffer
        (set-window-buffer (frame-root-window frame) (current-buffer))
        (insert-image (remoteterm-tests--pbm-image 20 10))
        (remote-display--record display)
        (redisplay t)
        (delete-terminal (get-device-terminal display))))
    (garbage-collect)
    (clear-image-cache t)
    'alive))

;;;; The loop of the trials

(defun remoteterm-tests--trials-until (trials bad)
  "Run TRIALS trials of the indexes from 0, which fail at the index BAD."
  (let ((rd-gen-trials trials)
        (index -1))
    (rd-gen-for-all (lambda () (cl-incf index))
                    (lambda (value) (should (/= value bad))))))

(defun remoteterm-tests--fail-at-3 ()
  "Run 10 trials, and fail at the trial with the index 3."
  (remoteterm-tests--trials-until 10 3))

(defun remoteterm-tests--pass-10 ()
  "Run 10 trials that pass."
  (remoteterm-tests--trials-until 10 -1))

(defun remoteterm-tests--zero-trials ()
  "Run a loop of 0 trials."
  (remoteterm-tests--trials-until 0 -1))

(defun remoteterm-tests--no-loop ()
  "Do nothing, so that no trial runs."
  nil)

(defun remoteterm-tests--fail-on-frame ()
  "Fail at a trial with a value that `read' cannot read."
  (let ((rd-gen-trials 1))
    (rd-gen-for-all #'selected-frame (lambda (frame) (should-not frame)))))

(defun remoteterm-tests--fail-on-random-value ()
  "Fail at the trial with the index 5 of random 32-bit integers."
  (let ((rd-gen-trials 8)
        (index -1))
    (rd-gen-for-all #'rd-gen-uint32
                    (lambda (_) (should (/= (cl-incf index) 5))))))

(defun remoteterm-tests--crash ()
  "Stop this Emacs at once, as a crash stops it."
  (signal-process (emacs-pid) 'SIGKILL))

(defun remoteterm-tests--crash-in-check ()
  "Run 10 trials, and crash in the check of the trial with the index 3."
  (let ((rd-gen-trials 10)
        (index -1))
    (rd-gen-for-all (lambda () (cl-incf index))
                    (lambda (value)
                      (when (= value 3) (remoteterm-tests--crash))))))

(defun remoteterm-tests--crash-in-generator ()
  "Run 10 trials, and crash in the generator of the trial with the index 3."
  (let ((rd-gen-trials 10)
        (index -1))
    (rd-gen-for-all (lambda ()
                      (when (= (cl-incf index) 3) (remoteterm-tests--crash)))
                    #'ignore)))

(defun remoteterm-tests--crash-after-loop ()
  "Run 3 trials that pass, and then crash."
  (remoteterm-tests--trials-until 3 -1)
  (remoteterm-tests--crash))

(defconst remoteterm-tests--non-ascii-value
  (list (concat "\N{LATIN SMALL LETTER E WITH ACUTE}\r\n"
                "\N{DEVANAGARI LETTER KA}\N{DEVANAGARI SIGN VIRAMA}"
                "\N{DEVANAGARI LETTER SSA}")
        (intern (string (decode-char 'eight-bit #xc3)
                        (decode-char 'eight-bit #xa9))))
  "A value with a printed form that has non-ASCII text and raw bytes.
The raw bytes #xc3 #xa9 are also the UTF-8 form of a character, so a
file that holds them as single bytes changes them.")

(defun remoteterm-tests--crash-on-non-ascii ()
  "Crash in a trial whose value is not ASCII."
  (rd-gen-for-all (lambda () remoteterm-tests--non-ascii-value)
                  (lambda (_) (remoteterm-tests--crash))))

(defun remoteterm-tests--fail-on-non-ascii ()
  "Fail in a trial whose value is not ASCII."
  (rd-gen-for-all (lambda () remoteterm-tests--non-ascii-value)
                  (lambda (_) (should nil))))

(defun remoteterm-tests--collections-at-delete-form ()
  "Return a form for the daemon that draws a frame and deletes it.
The value of the form is the number of collections before the frame
goes."
  `(let* ((before gcs-done)
          (collections nil)
          (record (lambda (_) (setq collections (- gcs-done before)))))
     (add-hook 'delete-frame-functions record 100)
     (unwind-protect ,(remoteterm-tests--draw-text-form "x")
       (remove-hook 'delete-frame-functions record))
     collections))

(defun remoteterm-tests--result-with-collector (collector)
  "Return the result of a daemon test with COLLECTOR as `garbage-collect'.
COLLECTOR is a form that gives a function."
  (ert-run-test
   (make-ert-test
    :body (lambda ()
            (rd-with-daemon
              (rd-daemon-eval `(progn (fset 'garbage-collect ,collector)
                                      nil)))))))

(defun remoteterm-tests--write-fixture (file word)
  "Write to FILE a definition of a function that signals an error with WORD.
The file loads the library that a daemon needs for a report."
  (with-temp-file file
    (insert ";;; -*- lexical-binding:t -*-\n")
    (prin1 `(load ,(file-name-sans-extension
                    (symbol-file 'rd-gen-report 'defun))
                  nil t)
           (current-buffer))
    (prin1 `(defun remoteterm-tests--fixture () (error ,word))
           (current-buffer))))

(rd-daemon-deftest remoteterm-tests--failing-trial
  "Fail at a trial.  `remoteterm-tests-deftest-failure' runs this test."
  (remoteterm-tests--fail-at-3))

(defconst remoteterm-tests--failing-trial-test
  (ert-get-test 'remoteterm-tests--failing-trial)
  "A test of `rd-daemon-deftest' that must fail.")

;; This test must fail, so it is not in the tests of this file.
(ert-delete-test 'remoteterm-tests--failing-trial)

(defun remoteterm-tests--daemon-result (function)
  "Return the result of an ERT test that calls `rd-daemon-check' on FUNCTION."
  (ert-run-test (make-ert-test
                 :body (lambda () (rd-daemon-check function)))))

(rd-daemon-deftest remoteterm-tests-trials-in-daemon
  "The generators and the checks of a daemon test run in the daemon."
  (let ((rd-gen-trials 5))
    (rd-gen-for-all (lambda () (should (daemonp)) (rd-gen-uint16))
                    (lambda (_) (should (daemonp))))))

(rd-daemon-deftest remoteterm-tests-daemon-gc-threshold
  "The daemon does not collect garbage after each frame of a trial."
  (should (= gc-cons-threshold rd-daemon-gc-threshold))
  (rd-gen-for-all #'rd-gen-uint16 #'ignore))

(ert-deftest remoteterm-tests-report-first-failure ()
  "A report counts the trials before the first failure, and names that trial."
  (skip-unless (featurep 'remote-test))
  (rd-gen-for-all
   (lambda ()
     (let ((trials (rd-gen-integer 1 20)))
       (cons trials (rd-gen-integer 0 (+ trials 2)))))
   (lambda (case)
     (pcase-let* ((`(,count . ,bad) case)
                  ((map :trials :trial :failure)
                   (rd-gen-report
                    (lambda ()
                      (remoteterm-tests--trials-until count bad)))))
       (if (< bad count)
           (progn
             (should (= trials bad))
             (should (equal trial (format "%d: %d" bad bad)))
             (should (string-search "ert-test-failed" failure)))
         (should (= trials count))
         (should-not trial)
         (should-not failure))))))

(ert-deftest remoteterm-tests-report-failure-outside-trial ()
  "A failure that is not in a trial has a report with no trial."
  (skip-unless (featurep 'remote-test))
  (pcase-let (((map :trials :trial :failure)
               (rd-gen-report (lambda () (error "No trials")))))
    (should (= trials 0))
    (should-not trial)
    (should (string-search "No trials" failure))))

(ert-deftest remoteterm-tests-report-failure-after-loop ()
  "A failure after the last trial has a report with no trial."
  (skip-unless (featurep 'remote-test))
  (pcase-let (((map :trials :trial :failure)
               (rd-gen-report (lambda ()
                                (remoteterm-tests--trials-until 2 -1)
                                (error "After the loop")))))
    (should (= trials 2))
    (should-not trial)
    (should (string-search "After the loop" failure))))

(ert-deftest remoteterm-tests-report-failure-in-generator ()
  "A failure in a generator names the index of its trial."
  (skip-unless (featurep 'remote-test))
  (pcase-let (((map :trials :trial :failure)
               (rd-gen-report
                (lambda ()
                  (let ((rd-gen-trials 5)
                        (index -1))
                    (rd-gen-for-all (lambda ()
                                      (when (= (cl-incf index) 2)
                                        (error "In the generator")))
                                    #'ignore))))))
    (should (= trials 2))
    (should (equal trial "2"))
    (should (string-search "In the generator" failure))))

(ert-deftest remoteterm-tests-zero-trials ()
  "A loop that finishes no trial fails."
  (skip-unless (featurep 'remote-test))
  (should-error (remoteterm-tests--zero-trials) :type 'ert-test-failed))

(ert-deftest remoteterm-tests-daemon-report ()
  "The daemon runs all trials, and the batch Emacs gets the report."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (= (plist-get (rd-daemon-report #'remoteterm-tests--pass-10)
                          :trials)
               10))))

(ert-deftest remoteterm-tests-daemon-same-values ()
  "The daemon generates the values of the batch Emacs."
  (skip-unless (featurep 'remote-test))
  (let ((batch (rd-gen-report #'remoteterm-tests--fail-on-random-value)))
    (should (plist-get batch :trial))
    (rd-with-daemon
      (should (equal (plist-get (rd-daemon-report
                                 #'remoteterm-tests--fail-on-random-value)
                                :trial)
                     (plist-get batch :trial))))))

(ert-deftest remoteterm-tests-daemon-pass ()
  "A test passes if all trials pass in the daemon."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (ert-test-passed-p
             (remoteterm-tests--daemon-result #'remoteterm-tests--pass-10)))))

(ert-deftest remoteterm-tests-daemon-failure ()
  "A failure in the daemon fails the test, and the test names the trial."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (let ((result (remoteterm-tests--daemon-result
                   #'remoteterm-tests--fail-at-3)))
      (should (ert-test-failed-p result))
      (should (equal (ert-test-result-with-condition-infos result)
                     '(("Trial: " . "3: 3")))))))

(ert-deftest remoteterm-tests-daemon-failure-non-ascii ()
  "A failed trial with a value that is not ASCII has the correct name."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (equal (ert-test-result-with-condition-infos
                    (remoteterm-tests--daemon-result
                     #'remoteterm-tests--fail-on-non-ascii))
                   `(("Trial: "
                      . ,(format "0: %S"
                                 remoteterm-tests--non-ascii-value)))))))

(ert-deftest remoteterm-tests-daemon-failure-unreadable-value ()
  "A trial fails with a value that has no printed form that `read' can read."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (let ((result (remoteterm-tests--daemon-result
                   #'remoteterm-tests--fail-on-frame)))
      (should (ert-test-failed-p result))
      (should (string-match-p
               "\\`0: #<frame"
               (cdar (ert-test-result-with-condition-infos result)))))))

(ert-deftest remoteterm-tests-daemon-crash ()
  "A crash of the daemon fails the test, and the test names the trial."
  (skip-unless (featurep 'remote-test))
  (pcase-dolist (`(,function . ,trial)
                 `((remoteterm-tests--crash-in-check . "3: 3")
                   (remoteterm-tests--crash-in-generator . "3")
                   (remoteterm-tests--crash-after-loop . "none")
                   (remoteterm-tests--crash-on-non-ascii
                    . ,(format "0: %S" remoteterm-tests--non-ascii-value))))
    (ert-info ((symbol-name function) :prefix "Function: ")
      (rd-with-daemon
        (let ((result (remoteterm-tests--daemon-result function)))
          (should (ert-test-failed-p result))
          (should (equal (ert-test-result-with-condition-infos result)
                         `(("Trial: " . ,trial)))))))))

(ert-deftest remoteterm-tests-daemon-crash-after-failure ()
  "A crash outside a trial does not name the trial of an older failure."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (remoteterm-tests--daemon-result #'remoteterm-tests--fail-at-3)
    (should (equal (ert-test-result-with-condition-infos
                    (remoteterm-tests--daemon-result
                     #'remoteterm-tests--crash))
                   '(("Trial: " . "none"))))))

(ert-deftest remoteterm-tests-deftest-failure ()
  "A test of `rd-daemon-deftest' fails if a trial fails, and names the trial."
  (skip-unless (featurep 'remote-test))
  (let ((result (ert-run-test remoteterm-tests--failing-trial-test)))
    (should (ert-test-failed-p result))
    (should (equal (ert-test-result-with-condition-infos result)
                   '(("Trial: " . "3: 3"))))))

(ert-deftest remoteterm-tests-daemon-garbage ()
  "The daemon collects when a drawn frame goes, in a test with no report."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (should (> (rd-daemon-eval (remoteterm-tests--collections-at-delete-form))
               0))))

(ert-deftest remoteterm-tests-daemon-garbage-required ()
  "A test fails if the daemon does not collect before it stops."
  (skip-unless (featurep 'remote-test))
  (should (ert-test-failed-p
           (remoteterm-tests--result-with-collector '#'ignore))))

(ert-deftest remoteterm-tests-daemon-garbage-crash ()
  "A test fails if the daemon crashes in the last collection."
  (skip-unless (featurep 'remote-test))
  (should (ert-test-failed-p
           (remoteterm-tests--result-with-collector
            '(lambda () (signal-process (emacs-pid) 'SIGKILL))))))

(ert-deftest remoteterm-tests-daemon-loads-newest-file ()
  "The daemon runs the source file of a test if it is newer than the .elc file."
  (skip-unless (featurep 'remote-test))
  (let* ((directory (make-temp-file "rd-fixture" t))
         (file (expand-file-name "rd-fixture.el" directory)))
    (unwind-protect
        (progn
          (remoteterm-tests--write-fixture file "old body")
          (should (byte-compile-file file))
          (remoteterm-tests--write-fixture file "new body")
          (set-file-times file (time-add nil 60))
          (load file nil t t)
          (rd-with-daemon
            (should (string-search
                     "new body"
                     (plist-get (rd-daemon-report 'remoteterm-tests--fixture)
                                :failure)))))
      (delete-directory directory t))))

(ert-deftest remoteterm-tests-daemon-no-trial ()
  "A test fails if no trial ran in the daemon."
  (skip-unless (featurep 'remote-test))
  (rd-with-daemon
    (dolist (function '(remoteterm-tests--no-loop
                        remoteterm-tests--zero-trials))
      (ert-info ((symbol-name function) :prefix "Function: ")
        (should (ert-test-failed-p
                 (remoteterm-tests--daemon-result function)))))))

(provide 'remoteterm-tests)
;;; remoteterm-tests.el ends here
