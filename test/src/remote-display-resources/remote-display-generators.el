;;; remote-display-generators.el --- generators for property tests  -*- lexical-binding:t -*-

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

;; A small library for property tests of the remote display code.
;; ERT has no generators, so this file supplies them.  The generators
;; use their own seeded random state, so a failure is reproducible
;; and the tests do not change the random state of other tests.  A
;; loop of trials can run in another Emacs: `rd-gen-report' gives its
;; result as a plist, and `rd-gen-check-report' gives that result to
;; ERT.  If that Emacs crashes, it gives no result, so `rd-gen-report'
;; can also keep the name of the current trial in a file, and
;; `rd-gen-last-trial' reads it.

;;; Code:

(require 'cl-lib)
(require 'ert)
(require 'seq)

(defconst rd-gen-trials 500
  "Number of generated values for each property test.")

(defvar rd-gen--random-state nil
  "The random state of the generators.")

(defvar rd-gen--finished-trials 0
  "The number of trials that the last `rd-gen-for-all' finished.")

(defvar rd-gen--current-trial nil
  "The name of the trial that runs now, or nil.")

(defvar rd-gen--trial-file nil
  "The file that holds the name of the current trial, or nil.")

(defun rd-gen-for-all (generate check)
  "Call CHECK with `rd-gen-trials' values from GENERATE.
Use a fixed seed.  The trial and the value that failed are in the
ERT message.  It is a failure if no trial finishes, because then the
loop tests nothing."
  (let ((rd-gen--random-state (cl-make-random-state 1)))
    (setq rd-gen--finished-trials 0)
    (dotimes (trial rd-gen-trials)
      (rd-gen--set-current-trial (number-to-string trial))
      (ert-info (rd-gen--current-trial :prefix "Trial: ")
        (let ((value (funcall generate)))
          (rd-gen--set-current-trial (format "%d: %S" trial value))
          (ert-info (rd-gen--current-trial :prefix "Trial: ")
            (funcall check value))))
      (rd-gen--set-current-trial nil)
      (cl-incf rd-gen--finished-trials)))
  (should (> rd-gen--finished-trials 0)))

(defun rd-gen--set-current-trial (name)
  "Make NAME the name of the current trial.
NAME is nil if no trial runs.  The file of the trial must survive a
crash of this Emacs, not a crash of the machine, so no fsync is
necessary.  The file holds the printed form of NAME, because a coding
system changes raw bytes that have the form of a UTF-8 character, and
the printed form of a string has escapes for raw bytes."
  (setq rd-gen--current-trial name)
  (when rd-gen--trial-file
    (if name
        (let ((coding-system-for-write 'utf-8-emacs-unix)
              (write-region-inhibit-fsync t))
          (write-region (prin1-to-string name) nil
                        rd-gen--trial-file nil 'silent))
      (delete-file rd-gen--trial-file))))

(defun rd-gen-report (function &optional trial-file)
  "Call FUNCTION, which runs `rd-gen-for-all', and return a plist.
:trials is the number of trials that finished.  After a failure,
:failure is the printed condition, and :trial names the trial that
failed, or is nil if the failure is not in a trial.  Both are strings,
so another Emacs can read the plist, whatever the values of the trials
are.  If TRIAL-FILE is non-nil, keep the name of the current trial in
that file, for `rd-gen-last-trial'."
  (let ((rd-gen--finished-trials 0)
        (rd-gen--current-trial nil)
        (rd-gen--trial-file trial-file))
    (condition-case failure
        (progn
          (funcall function)
          (list :trials rd-gen--finished-trials))
      (error (list :trials rd-gen--finished-trials
                   :trial rd-gen--current-trial
                   :failure (format "%S" failure))))))

(defun rd-gen-last-trial (trial-file)
  "Return the name of the trial that TRIAL-FILE holds, or nil.
TRIAL-FILE is as for `rd-gen-report'.  It holds a name while a trial
runs, so this name remains after a crash in the trial."
  (when (file-exists-p trial-file)
    (with-temp-buffer
      (let ((coding-system-for-read 'utf-8-emacs-unix))
        (insert-file-contents trial-file))
      (read (current-buffer)))))

(defun rd-gen-check-report (report)
  "Fail the current ERT test if REPORT has a failure or no finished trial.
REPORT is a value of `rd-gen-report'.  The ERT message names the trial."
  (let ((failure (plist-get report :failure)))
    (when failure
      (ert-info ((or (plist-get report :trial) "none") :prefix "Trial: ")
        (ert-fail failure))))
  (should (> (plist-get report :trials) 0)))

(defun rd-gen-random (limit)
  "Return a random integer from 0 to LIMIT - 1."
  (cl-random limit rd-gen--random-state))

(defun rd-gen-integer (low high)
  "Return a random integer from LOW to HIGH, often LOW or HIGH."
  (pcase (rd-gen-random 4)
    (0 low)
    (1 high)
    (_ (+ low (rd-gen-random (1+ (- high low)))))))

(defun rd-gen-uint16 ()
  "Return a random 16-bit unsigned integer, often a boundary value."
  (rd-gen-integer 0 #xffff))

(defun rd-gen-uint32 ()
  "Return a random 32-bit unsigned integer, often a boundary value."
  (rd-gen-integer 0 #xffffffff))

(defun rd-gen-int32 ()
  "Return a random signed 32-bit integer, often at a limit of a width.
The limits of 8, 16, and 32 bits find a field that is too narrow."
  (rd-gen-element (list (- (ash 1 31)) (1- (ash 1 31))
                        -65536 -32769 -32768 -256 -1 0 1 255
                        32767 32768 65535 65536
                        (- (rd-gen-uint32) (ash 1 31))
                        (- (rd-gen-uint32) (ash 1 31)))))

(defun rd-gen-element (list)
  "Return a random element of LIST."
  (nth (rd-gen-random (length list)) list))

(defun rd-gen-list (generate max-length)
  "Return a list of up to MAX-LENGTH values from GENERATE."
  (cl-loop repeat (rd-gen-random (1+ max-length))
           collect (funcall generate)))

(defun rd-gen-bytes (max-length)
  "Return a random unibyte string of up to MAX-LENGTH bytes."
  (apply #'unibyte-string (rd-gen-list (lambda () (rd-gen-random 256))
                                       max-length)))

(defun rd-gen-parts (size max-parts)
  "Return a random cut of SIZE into from 1 to MAX-PARTS parts.
SIZE is a positive integer.  The value is a list of (START . LENGTH),
in order.  The first part starts at 0, each part starts where the one
before it ends, and the last part ends at SIZE."
  (let* ((cuts (seq-uniq
                (sort (cl-loop repeat (rd-gen-random (min max-parts size))
                               collect (1+ (rd-gen-random (1- size))))
                      #'<)))
         (starts (cons 0 cuts)))
    (cl-mapcar (lambda (start end) (cons start (- end start)))
               starts (append cuts (list size)))))

(defun rd-gen-tool-bar-margin ()
  "Return a random value of `tool-bar-button-margin'.
The value is a number from 0 to 6, or a pair (X . Y) of two different
numbers from 0 to 6."
  (if (= (rd-gen-random 2) 0)
      (rd-gen-integer 0 6)
    (let* ((x (rd-gen-random 7))
           (y (mod (+ x 1 (rd-gen-random 6)) 7)))
      (cons x y))))

(defun rd-gen-child-frame-ops (max-length)
  "Return a list of up to MAX-LENGTH random operations on child frames.
An index I names a frame in a list that the test keeps: 0 is a root
frame, and the children follow in the order that the operations make
them.  The test takes I modulo the length of its list.  Each operation
is one of these lists:
  (make I LEFT TOP VISIBLE)  make a child of frame I at LEFT, TOP.
  (move I X Y)               call `set-frame-position' on frame I.
  (visible I FLAG)           show frame I if FLAG, else hide it.
  (raise I) and (lower I)    raise or lower frame I.
  (reparent I J)             give frame I the parent J, or no parent
                             if J is nil.
  (delete I)                 delete frame I.  I is not 0.
  (show I)                   make the client know frame I.
A position can be negative."
  (rd-gen-list #'rd-gen--child-frame-op max-length))

(defun rd-gen--child-frame-op ()
  "Return one random operation of `rd-gen-child-frame-ops'."
  (cl-flet ((index () (rd-gen-integer 0 7))
            (offset () (rd-gen-integer -60 60))
            (flag () (= (rd-gen-random 2) 0)))
    (pcase (rd-gen-random 8)
      (0 (list 'make (index) (offset) (offset) (flag)))
      (1 (list 'move (index) (offset) (offset)))
      (2 (list 'visible (index) (flag)))
      (3 (list 'raise (index)))
      (4 (list 'lower (index)))
      (5 (list 'reparent (index) (and (flag) (index))))
      (6 (list 'delete (rd-gen-integer 1 7)))
      (7 (list 'show (index))))))

(defun rd-gen-tip-text (max-lines max-length)
  "Return a random text of 1 to MAX-LINES lines for a tooltip.
Each line has 1 to MAX-LENGTH lowercase letters.  The text has no
newline at its end."
  (mapconcat (lambda (_)
               (apply #'string
                      (cl-loop repeat (rd-gen-integer 1 max-length)
                               collect (+ ?a (rd-gen-random 26)))))
             (number-sequence 1 (rd-gen-integer 1 max-lines))
             "\n"))

(defun rd-gen-tip-axis ()
  "Return a random case for one axis of the place of a tooltip.
The value is (MOUSE SIZE LIMIT OFFSET EXPLICIT).  LIMIT is the size of
the root frame on the axis, and SIZE is the size of the tooltip, which
can be larger than LIMIT.  MOUSE can be outside the root frame.
OFFSET is nil or a positive number of pixels.  EXPLICIT is nil, or a
position that the parameters of the tooltip give."
  (let* ((limit (rd-gen-integer 1 2000))
         (size (rd-gen-integer 1 (+ limit 100))))
    (list (rd-gen-integer -100 (+ limit 100))
          size
          limit
          (and (/= (rd-gen-random 4) 0) (rd-gen-integer 1 40))
          (and (= (rd-gen-random 4) 0) (rd-gen-integer -100 2000)))))

(defun rd-gen-frame-defaults ()
  "Return a random alist of frame defaults for `default-frame-alist'.
Each entry asks for a bar, a fullscreen state or a large size, which
a tooltip must not get."
  (cl-remove-if-not (lambda (_) (= (rd-gen-random 2) 0))
                    '((fullscreen . maximized) (menu-bar-lines . 1)
                      (tab-bar-lines . 1) (tool-bar-lines . 1)
                      (width . 200) (height . 80)
                      (vertical-scroll-bars . right)
                      (horizontal-scroll-bars . t))))

(defun rd-gen-frame-kind ()
  "Return a random kind of frame: normal, child or minibuffer-only.
A normal frame is a root frame with a minibuffer and windows.  A child
frame has a parent frame.  A minibuffer-only frame has only a
minibuffer window."
  (rd-gen-element '(normal child minibuffer-only)))

(defun rd-gen-child-chain (max-length)
  "Return the positions of a random chain of up to MAX-LENGTH child frames.
The value is a list of (LEFT . TOP), one for each frame of the chain,
from the frame nearest to the root.  Each LEFT and TOP is from 1 to 30
pixels, so that each frame of the chain moves the origin."
  (rd-gen-list (lambda () (cons (rd-gen-integer 1 30) (rd-gen-integer 1 30)))
               max-length))

(defun rd-gen-text (max-length)
  "Return a random string of up to MAX-LENGTH characters.
The characters have 1, 2, 3, or 4 bytes of UTF-8."
  (apply #'string (rd-gen-list (lambda () (rd-gen-element '(?a ?é ?€ ?😀)))
                               max-length)))

;;;; Menus

(defconst rd-gen-menu-separators '("--" "--single-line")
  "The labels that the menu generators use for a separator.")

(defun rd-gen-menu ()
  "Return a random menu for `x-popup-menu'.
The menu is a keymap menu or a list of panes.  A keymap menu has
symbols as keys, and nested keymaps as submenus.  Its items can be
disabled, toggles, or radios, and can have `:keys'.  A list of panes
is (TITLE (PANE-NAME ITEM...)...), and an item is (LABEL . VALUE), a
string, or nil.  A keymap menu has at least one binding, and a pane
has at least one item.  Some labels have characters that are not
ASCII, and some have more than 1024 bytes of UTF-8."
  (if (zerop (rd-gen-random 3))
      (rd-gen-menu--panes)
    (rd-gen-menu--keymap 2 (and (zerop (rd-gen-random 2))
                                (rd-gen-menu-text)))))

(defun rd-gen-menu-text ()
  "Return a random text for a menu label or title.
The text is not empty, and it does not start with \"-\" or \"@\",
because these characters make a separator or a pane of a keymap.  Some
texts have more than 1024 bytes of UTF-8."
  (let ((characters '(?a ?Z ?0 ?\s ?é ?€ ?日 ?😀)))
    (apply #'string
           (rd-gen-element characters)
           (cl-loop repeat (if (zerop (rd-gen-random 5))
                               (+ 500 (rd-gen-random 300))
                             (rd-gen-random 12))
                    collect (rd-gen-element characters)))))

(defun rd-gen-large-menu ()
  "Return a random keymap menu with some hundreds of bindings.
The bindings are as in `rd-gen-menu', with submenus to one level, and
each label has some hundreds of bytes of UTF-8 or more."
  (rd-gen-menu--keymap 1 nil (+ 200 (rd-gen-random 200))
                       #'rd-gen-menu--long-text))

(defun rd-gen-menu--long-text ()
  "Return a random menu text with some hundreds of characters or more."
  (concat (make-string (+ 100 (rd-gen-random 200)) ?a) (rd-gen-menu-text)))

(defun rd-gen-menu--keymap (depth prompt &optional count text)
  "Return a random keymap menu with submenus to DEPTH levels.
PROMPT is the prompt string of the keymap, or nil.  COUNT is the number
of bindings of the top level, or nil for one to six.  TEXT returns
the labels, or nil for `rd-gen-menu-text'."
  `(keymap ,@(and prompt (list prompt))
           ,@(cl-loop for index from 0
                      repeat (or count (1+ (rd-gen-random 6)))
                      collect (cons (intern (format "item-%d" index))
                                    (rd-gen-menu--keymap-entry
                                     depth (or text #'rd-gen-menu-text))))))

(defun rd-gen-menu--keymap-entry (depth text)
  "Return a random menu-item binding of a keymap.
The binding is an item, a separator, or (when DEPTH is above 0) a
submenu with entries to DEPTH - 1 levels.  TEXT returns the labels."
  (pcase (rd-gen-random (if (> depth 0) 6 5))
    (0 `(menu-item ,(rd-gen-element rd-gen-menu-separators)))
    (5 `(menu-item ,(funcall text)
                   ,(rd-gen-menu--keymap (1- depth) nil nil text)
                   ,@(rd-gen-menu--enable)))
    (_ `(menu-item ,(funcall text) rd-gen-menu-command
                   ,@(rd-gen-menu--enable)
                   ,@(rd-gen-menu--button)
                   ,@(rd-gen-menu--keys)))))

(defun rd-gen-menu--enable ()
  "Return random `:enable' properties: none, or `:enable nil'."
  (and (zerop (rd-gen-random 4)) (list :enable nil)))

(defun rd-gen-menu--button ()
  "Return random `:button' properties: none, a toggle, or a radio."
  (pcase (rd-gen-random 3)
    (0 nil)
    (1 (list :button (cons :toggle (rd-gen-element '(nil t)))))
    (_ (list :button (cons :radio (rd-gen-element '(nil t)))))))

(defun rd-gen-menu--keys ()
  "Return random `:keys' properties.
A key text has only letters and spaces, so `substitute-command-keys'
does not change it.  Some key texts have more than 128 bytes of
UTF-8."
  (pcase (rd-gen-random 4)
    (0 (list :keys (rd-gen-element '("C-c a" "M-x b" "x"))))
    (1 (list :keys (make-string (+ 40 (rd-gen-random 30)) ?é)))
    (_ nil)))

(defun rd-gen-menu--panes ()
  "Return a random menu as a list of panes, with one to three panes."
  (cons (rd-gen-element (list "" (rd-gen-menu-text)))
        (cl-loop repeat (1+ (rd-gen-random 3))
                 collect (cons (rd-gen-element (list "" (rd-gen-menu-text)))
                               (cl-loop repeat (1+ (rd-gen-random 5))
                                        collect (rd-gen-menu--pane-item))))))

(defun rd-gen-menu--pane-item ()
  "Return a random item of a pane.
The item is (LABEL . VALUE), a string (a disabled item), a separator
string, or nil (a boundary of a dialog, which a menu ignores)."
  (pcase (rd-gen-random 6)
    (0 (rd-gen-menu-text))
    (1 (rd-gen-element rd-gen-menu-separators))
    (2 nil)
    (_ (cons (rd-gen-menu-text) (rd-gen-random 1000)))))

(provide 'remote-display-generators)
;;; remote-display-generators.el ends here
