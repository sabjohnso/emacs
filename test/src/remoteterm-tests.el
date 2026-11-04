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
;;
;; 32. Trials: a test that runs its trials in a daemon fails if a trial
;;     fails, and the failure names the trial.  This is also true if
;;     the daemon crashes in the trial or in the generator of its value.
;;     A failure outside a trial names no trial.  The test also fails
;;     if no trial finishes.  The name of a trial is correct for each
;;     value, also for non-ASCII text and raw bytes.  The daemon
;;     generates the values that a batch Emacs generates, and it runs
;;     the newest form of the file of the test.
;; 30. Daemon garbage: each daemon of a test collects garbage when it
;;     deletes its first frame, so that frame is live and has drawn.
;;     It collects again after the body of the test.  The test fails if
;;     the daemon does not do that last collection, or crashes in it.
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
(declare-function remote-display--flush-display "remotetest.c" (frame))
(declare-function remote-display--kboard-keymap "remotetest.c" (display))
(declare-function remote-display--live-sinks "remotetest.c" ())
(declare-function remote-display--malloc-used "remotetest.c" ())
(declare-function remote-display--mouse-frame "remotetest.c" (display))
(declare-function remote-display--record "remotetest.c" (display))
(declare-function remote-display--scroll-run "remotetest.c"
                  (window current-y desired-y height))
(declare-function remote-display--set-last-frame-id "remotetest.c"
                  (display id))
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

(defun remoteterm-tests--make-frame (display parameters)
  "Make a frame on DISPLAY with PARAMETERS.
`make-frame' makes no graphical frames in batch mode, so call the
function that it calls for graphical frames."
  (x-create-frame-with-faces (cons (cons 'display display) parameters)))

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
  "Return the draw-glyphs messages in BYTES on the baseline Y."
  (seq-filter (lambda (message)
                (and (eq (car message) 'draw-glyphs)
                     (= (plist-get (cdr message) :y) y)))
              (remoteterm-tests--messages bytes)))

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
  "With no client, the hooks of redisplay run and a session has no sink.
The cursor state shows that redisplay reached the hooks.  A client that
comes later gets the text when a redraw sends it."
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
      (should cursor)
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
parameters of the frame.  The form evaluates the form SETUP with the
buffer current.  SETUP must insert text, and can use the variables
FRAME and WINDOW.  The value of the form is the value of the form
RESULT.  RESULT can use the variable OUTPUT, the bytes that the session
sent for the first draw, the variables DISPLAY and BUFFER, and the
variables of `remoteterm-tests--geometry-form'.  FONT is the font of
the last character that is not a newline."
  `(let* ((display "remote-draw")
          (frame (progn (x-open-connection display)
                        (x-create-frame-with-faces
                         '((display . "remote-draw") ,@parameters))))
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
that a draw-glyphs message drew on it."
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
         (remoteterm-tests--set-cell screen row (+ column i) glyph))
       (plist-get fields :glyphs)))))

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
of the text area, and :glyph-width is the width of a glyph.  Each glyph is 3 times the height of the frame font, and
the window scrolls to the left by HSCROLL columns."
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
the text area, and :width is the width of a column."
  (remoteterm-tests--frame-form
   '((width . 40))
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

;;;; Frame memory and focus

(defconst remoteterm-tests--memory-cycles 300
  "The number of frames that a test of the frame memory measures.")

(defconst remoteterm-tests--memory-limit 10000
  "The most kilobytes that `remoteterm-tests--memory-cycles' frames can add.")

(defun remoteterm-tests--resident-memory ()
  "Return the resident memory of this process in kilobytes, or nil."
  (alist-get 'rss (process-attributes (emacs-pid))))

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
                (remoteterm-tests--with-frame _frame "remote-kboard" nil)))
             remoteterm-tests--kboard-cycles)))

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
