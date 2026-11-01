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
;; laws:
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
;;    of glyphs starts where the run before it ends.
;; 7. Clear: after a line becomes shorter, replay the messages in
;;    order on the columns of the row.  Each column of the new text
;;    has its glyph, and each column after it is cleared or has a
;;    space.
;; 8. Scroll: after a change of the start of a window, a replay of
;;    all messages in order shows the lines from the new start.
;; 9. Scroll bounds: the source and the destination of each copy-area
;;    message are inside the text area, so a copy does not change the
;;    mode line.  A change of the start by fewer lines than the text
;;    area has sends at least one copy-area message.
;;
;; Redisplay does not run in batch mode, so the drawing tests run
;; their frames in a daemon.

;;; Code:

(require 'cl-lib)
(require 'ert)
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
  (skip-unless (featurep 'remote))
  (remoteterm-tests--with-display "remote-test"
    (should (eq (terminal-live-p (remoteterm-tests--terminal "remote-test"))
                'remote))))

(ert-deftest remoteterm-tests-close-connection ()
  (skip-unless (featurep 'remote))
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
  (skip-unless (featurep 'remote))
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
  (skip-unless (featurep 'remote))
  (remoteterm-tests--with-display "remote-test"
    (remoteterm-tests--with-frame frame "remote-test" nil
      (should-error (x-close-connection "remote-test"))
      (should (remoteterm-tests--terminal "remote-test")))))

;;;; Frames

(ert-deftest remoteterm-tests-make-frame ()
  (skip-unless (featurep 'remote))
  (remoteterm-tests--with-display "remote-test"
    (remoteterm-tests--with-frame frame "remote-test"
        '((width . 80) (height . 25))
      (should (eq (framep frame) 'remote))
      (should (display-graphic-p frame))
      (should (= (frame-width frame) 80))
      (should (= (frame-height frame) 25)))))

(ert-deftest remoteterm-tests-frame-size ()
  (skip-unless (featurep 'remote))
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
  (skip-unless (featurep 'remote))
  (remoteterm-tests--with-display "remote-test"
    (remoteterm-tests--with-frame frame "remote-test"
        '((foreground-color . "red") (background-color . "#102030"))
      (should (equal (frame-parameter frame 'foreground-color) "red"))
      (should (equal (frame-parameter frame 'background-color) "#102030"))
      (should (equal (face-attribute 'default :foreground frame) "red")))))

(ert-deftest remoteterm-tests-frame-visibility ()
  (skip-unless (featurep 'remote))
  (remoteterm-tests--with-display "remote-test"
    (remoteterm-tests--with-frame frame "remote-test" nil
      (should (frame-visible-p frame))
      (make-frame-invisible frame t)
      (should-not (frame-visible-p frame))
      (make-frame-visible frame)
      (should (frame-visible-p frame)))))

;;;; Colors

(defun remoteterm-tests--rgb ()
  "Return a random list of three 8-bit components."
  (list (rd-gen-integer 0 255) (rd-gen-integer 0 255)
        (rd-gen-integer 0 255)))

(ert-deftest remoteterm-tests-hex-color-values ()
  (skip-unless (featurep 'remote))
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
  (skip-unless (featurep 'remote))
  (rd-gen-for-all
   #'remoteterm-tests--color-string
   (lambda (string)
     (should (eq (and (xw-color-defined-p string) t)
                 (and (xw-color-values string) t))))))

(ert-deftest remoteterm-tests-named-color-before-session ()
  "Named colors work before any session is open."
  (skip-unless (featurep 'remote))
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
      (let ((result (remote-display--decode (substring bytes start))))
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
The value of the form is (OUTPUT CODES ORIGIN CHAR-WIDTH).  OUTPUT is
the bytes that the session sent.  CODES is the vector of the glyph
codes of the frame font for TEXT.  ORIGIN is (X . Y), the start of the
baseline of the first line.  CHAR-WIDTH is the width of a character
of the font.  The frame is wide enough for TEXT on one line."
  `(let* ((display "remote-draw")
          (frame (progn (x-open-connection display)
                        (x-create-frame-with-faces
                         '((display . "remote-draw")
                           (width . ,(+ (length text) 20))))))
          (window (frame-root-window frame))
          (buffer (generate-new-buffer "draw")))
     (unwind-protect
         (progn
           (with-current-buffer buffer (insert ,text))
           (set-window-buffer window buffer)
           ;; Redisplay skips all frames while the initial frame is
           ;; selected.
           (select-frame frame)
           (remote-display--record display)
           (remote-display--take-output display)
           (redisplay t)
           (let ((font (with-current-buffer buffer (font-at 1 window)))
                 (edges (window-inside-pixel-edges window)))
             (list (remote-display--take-output display)
                   (vconcat (mapcar (lambda (glyph) (aref glyph 3))
                                    (font-get-glyphs font 0 ,(length text)
                                                     ,text)))
                   (cons (nth 0 edges)
                         (+ (nth 1 edges) (aref (query-font font) 4)))
                   (frame-char-width frame))))
       (delete-frame frame)
       (kill-buffer buffer))))

(defun remoteterm-tests--drawn-p (codes bytes)
  "Return non-nil if the glyph runs in BYTES draw CODES in sequence.
A run can continue after the text, for example with the space glyph
at the end of a buffer, and a long text can need more than one run."
  (cl-search codes (apply #'vconcat (remoteterm-tests--glyph-runs bytes))))

(defconst remoteterm-tests--split-lengths '(1 511 512 513 1024 1025)
  "Text lengths at the limits of the glyphs of one draw-glyphs message.")

(defun remoteterm-tests--text-length (max)
  "Return a random text length from 1 to MAX, often at a split limit."
  (if (zerop (rd-gen-random 2))
      (rd-gen-element (seq-filter (lambda (n) (<= n max))
                                  remoteterm-tests--split-lengths))
    (rd-gen-integer 1 max)))

(defun remoteterm-tests--text ()
  "Return a random line of printable ASCII text."
  (apply #'string
         (cl-loop repeat (remoteterm-tests--text-length 600)
                  collect (rd-gen-integer ?\s ?~))))

(ert-deftest remoteterm-tests-draw-text ()
  (skip-unless (featurep 'remote))
  (rd-with-daemon
    (let ((rd-gen-trials 50))
      (rd-gen-for-all
       #'remoteterm-tests--text
       (lambda (text)
         (pcase-let ((`(,output ,codes . ,_)
                      (rd-daemon-eval
                       (remoteterm-tests--draw-text-form text))))
           (should (remoteterm-tests--drawn-p codes output))))))))

(defun remoteterm-tests--line-runs (bytes y)
  "Return the draw-glyphs messages in BYTES on the baseline Y."
  (seq-filter (lambda (message)
                (and (eq (car message) 'draw-glyphs)
                     (= (plist-get (cdr message) :y) y)))
              (remoteterm-tests--messages bytes)))

(ert-deftest remoteterm-tests-draw-position ()
  (skip-unless (featurep 'remote))
  (rd-with-daemon
    (let ((rd-gen-trials 20))
      (rd-gen-for-all
       (lambda () (make-string (remoteterm-tests--text-length 1100) ?x))
       (lambda (text)
         (pcase-let* ((`(,output ,_ (,x0 . ,y0) ,char-width)
                       (rd-daemon-eval
                        (remoteterm-tests--draw-text-form text)))
                      (runs (remoteterm-tests--line-runs output y0)))
           (should runs)
           (should (= (plist-get (cdr (car runs)) :x) x0))
           (cl-loop for (run next) on runs while next
                    do (should (= (plist-get (cdr next) :x)
                                  (+ (plist-get (cdr run) :x)
                                     (* char-width
                                        (length (plist-get (cdr run)
                                                           :glyphs)))))))))))))

(defun remoteterm-tests--frame-of-text-form (texts)
  "Return a form for the daemon that draws each of TEXTS on a new frame.
The value of the form is (OUTPUT CODES...), with the glyph codes of
each text."
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
           (cons (remote-display--take-output display)
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

(ert-deftest remoteterm-tests-frame-ids ()
  "The frames of a session draw with distinct frame IDs, one per frame."
  (skip-unless (featurep 'remote))
  (rd-with-daemon
    (let ((rd-gen-trials 10))
      (rd-gen-for-all
       (lambda () (remoteterm-tests--distinct-texts (rd-gen-integer 2 6)))
       (lambda (texts)
         (pcase-let* ((`(,output . ,codes)
                       (rd-daemon-eval
                        (remoteterm-tests--frame-of-text-form texts)))
                      (ids (mapcar (lambda (c)
                                     (remoteterm-tests--frame-ids c output))
                                   codes)))
           (should (cl-every (lambda (frame-ids) (= (length frame-ids) 1))
                             ids))
           (should (= (length (delete-dups (apply #'append ids)))
                      (length texts)))))))))

(ert-deftest remoteterm-tests-no-client ()
  "A session with no client sends nothing."
  (skip-unless (featurep 'remote))
  (rd-with-daemon
    (should (equal (rd-daemon-eval
                    '(let* ((display "remote-draw")
                            (frame (progn (x-open-connection display)
                                          (x-create-frame-with-faces
                                           '((display . "remote-draw"))))))
                       (select-frame frame)
                       (redraw-frame frame)
                       (redisplay t)
                       (prog1 (remote-display--take-output display)
                         (delete-frame frame))))
                   ""))))

(defun remoteterm-tests--geometry-form ()
  "Return a form that gives the geometry of the text of WINDOW.
The form uses the variables FRAME, WINDOW and FONT.  Its value is
\(X0 TOP ASCENT CHAR-WIDTH LINE-HEIGHT): the pixel position of the
left edge and of the top of the text area, the ascent of FONT, the
width of a character, and the height of a line."
  '(let ((edges (window-inside-pixel-edges window)))
     (list (nth 0 edges) (nth 1 edges) (aref (query-font font) 4)
           (frame-char-width frame) (frame-char-height frame))))

(defun remoteterm-tests--shorten-form (old new)
  "Return a form for the daemon that draws OLD, and then NEW in its place.
The value of the form is (OUTPUT CODES SPACE GEOMETRY).  OUTPUT is
only the bytes that the session sent for NEW.  CODES is the vector of
the glyph codes of NEW, and SPACE is the glyph code of a space.
GEOMETRY is as for `remoteterm-tests--geometry-form'."
  `(let* ((display "remote-draw")
          (frame (progn (x-open-connection display)
                        (x-create-frame-with-faces
                         '((display . "remote-draw")
                           (width . ,(+ (length old) 20))))))
          (window (frame-root-window frame))
          (buffer (generate-new-buffer "draw")))
     (unwind-protect
         (progn
           (with-current-buffer buffer (insert ,old))
           (set-window-buffer window buffer)
           (select-frame frame)
           (remote-display--record display)
           (redisplay t)
           (remote-display--take-output display)
           (let ((font (with-current-buffer buffer (font-at 1 window))))
             (with-current-buffer buffer (erase-buffer) (insert ,new))
             (redisplay t)
             (cl-flet ((codes (text)
                         (vconcat (mapcar (lambda (glyph) (aref glyph 3))
                                          (font-get-glyphs font 0 (length text)
                                                           text)))))
               (list (remote-display--take-output display)
                     (codes ,new)
                     (aref (codes " ") 0)
                     ,(remoteterm-tests--geometry-form)))))
       (delete-frame frame)
       (kill-buffer buffer))))

(defun remoteterm-tests--replay (messages rows columns geometry)
  "Return the screen after MESSAGES, replayed in order.
The screen is a vector of ROWS rows of the text area, and each row is
a vector of COLUMNS cells.  GEOMETRY is as for
`remoteterm-tests--geometry-form'.  The state of a cell is nil if no
message changed it, `clear' if a clear-area message covered it, or
the code of the glyph that a draw-glyphs message drew on it."
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
          (_ (error "The model has no message type %s" (car message))))))))

(defun remoteterm-tests--replay-draw (screen fields geometry)
  "Put the glyphs of the draw-glyphs FIELDS into the cells of SCREEN."
  (pcase-let* ((`(,x0 ,top ,ascent ,char-width ,line-height) geometry)
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

(defun remoteterm-tests--replay-copy (screen fields geometry)
  "Copy the cells of SCREEN inside the rectangle of the copy-area FIELDS.
Copy them by the distance from :x, :y to :to-x, :to-y."
  (pcase-let* ((`(,_ ,_ ,_ ,char-width ,line-height) geometry)
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
  (pcase-let ((`(,x0 ,top ,_ ,char-width ,line-height) geometry)
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

(ert-deftest remoteterm-tests-clear ()
  (skip-unless (featurep 'remote))
  (rd-with-daemon
    (let ((rd-gen-trials 20))
      (rd-gen-for-all
       (lambda ()
         (let ((old (rd-gen-integer 1 100)))
           (cons (make-string old ?x)
                 (make-string (rd-gen-integer 0 (1- old)) ?y))))
       (lambda (texts)
         (pcase-let* ((`(,old . ,new) texts)
                      (`(,output ,codes ,space ,geometry)
                       (rd-daemon-eval
                        (remoteterm-tests--shorten-form old new)))
                      (row (aref (remoteterm-tests--replay
                                  (remoteterm-tests--messages output)
                                  1 (length old) geometry)
                                 0)))
           (should (equal (seq-take row (length new)) codes))
           (should (cl-every (lambda (state) (memq state (list 'clear space)))
                             (seq-drop row (length new))))))))))

(defconst remoteterm-tests--scroll-lines 60
  "The number of lines of the buffer of the scroll test.")

(defun remoteterm-tests--scroll-form (height extra from to)
  "Return a form for the daemon that scrolls a window of a new frame.
The frame has HEIGHT lines and EXTRA pixels, so that the last line can
be visible only in part.  The buffer has lines of 3 digits, the
numbers from 0.  The window starts at line FROM, and then at line TO.
The value of the form is (OUTPUT ROWS CODES GEOMETRY BOTTOM).  OUTPUT
is the bytes that the session sent for both starts.  ROWS is the number
of lines of the text area.  BOTTOM is the pixel position of the bottom
of the text area.  CODES is a list of the glyph codes of each
line.  GEOMETRY is as for `remoteterm-tests--geometry-form'."
  `(let* ((display "remote-draw")
          (frame (progn (x-open-connection display)
                        (x-create-frame-with-faces
                         '((display . "remote-draw")
                           (width . 20) (height . ,height)))))
          (window (frame-root-window frame))
          (buffer (generate-new-buffer "scroll")))
     (cl-flet ((start (line)
                 (with-current-buffer buffer
                   (goto-char (point-min))
                   (forward-line line)
                   (set-window-start window (point))
                   (set-window-point window (point)))))
       (unwind-protect
           (progn
             (with-current-buffer buffer
               (dotimes (i ,remoteterm-tests--scroll-lines)
                 (insert (format "%03d\n" i))))
             (set-window-buffer window buffer)
             (let ((frame-resize-pixelwise t))
               (set-frame-size frame (frame-text-width frame)
                               (+ (frame-text-height frame) ,extra) t))
             (select-frame frame)
             (start ,from)
             (remote-display--record display)
             (redisplay t)
             (start ,to)
             (redisplay t)
             (let ((font (with-current-buffer buffer (font-at 1 window))))
               (list (remote-display--take-output display)
                     (window-body-height window)
                     (with-current-buffer buffer
                       (mapcar (lambda (line)
                                 (vconcat
                                  (mapcar (lambda (glyph) (aref glyph 3))
                                          (font-get-glyphs font 0 3 line))))
                               (split-string (buffer-string) "\n" t)))
                     ,(remoteterm-tests--geometry-form)
                     (nth 3 (window-inside-pixel-edges window)))))
         (delete-frame frame)
         (kill-buffer buffer)))))

(defun remoteterm-tests--scroll-case ()
  "Return random arguments for `remoteterm-tests--scroll-form'."
  (list (rd-gen-integer 4 12) (rd-gen-integer 0 20)
        (rd-gen-integer 0 20) (rd-gen-integer 0 20)))

(ert-deftest remoteterm-tests-scroll ()
  "After a change of the window start, the screen shows the new lines."
  (skip-unless (featurep 'remote))
  (rd-with-daemon
    (let ((rd-gen-trials 20))
      (rd-gen-for-all
       #'remoteterm-tests--scroll-case
       (lambda (case)
         (pcase-let* ((`(,_ ,_ ,_ ,to) case)
                      (`(,output ,rows ,codes ,geometry . ,_)
                       (rd-daemon-eval
                        (apply #'remoteterm-tests--scroll-form case)))
                      (screen (remoteterm-tests--replay
                               (remoteterm-tests--messages output)
                               rows 3 geometry)))
           (should (equal (append screen nil)
                          (seq-subseq codes to (+ to rows))))))))))

(defun remoteterm-tests--copies-inside-p (messages top bottom)
  "Return non-nil if each copy-area in MESSAGES stays from TOP to BOTTOM.
Both the source and the destination of each copy must be inside."
  (cl-every (lambda (message)
              (let* ((fields (cdr message))
                     (height (plist-get fields :height)))
                (cl-every (lambda (y) (<= top y (+ y height) bottom))
                          (list (plist-get fields :y)
                                (plist-get fields :to-y)))))
            (seq-filter (lambda (message) (eq (car message) 'copy-area))
                        messages)))

(ert-deftest remoteterm-tests-scroll-bounds ()
  (skip-unless (featurep 'remote))
  (rd-with-daemon
    (let ((rd-gen-trials 20))
      (rd-gen-for-all
       #'remoteterm-tests--scroll-case
       (lambda (case)
         (pcase-let* ((`(,_ ,_ ,from ,to) case)
                      (`(,output ,rows ,_ (,_ ,top . ,_) ,bottom)
                       (rd-daemon-eval
                        (apply #'remoteterm-tests--scroll-form case)))
                      (messages (remoteterm-tests--messages output)))
           (should (remoteterm-tests--copies-inside-p messages top bottom))
           (when (< 0 (abs (- to from)) rows)
             (should (assq 'copy-area messages)))))))))

(provide 'remoteterm-tests)
;;; remoteterm-tests.el ends here
