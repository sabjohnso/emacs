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

;;; Code:

(require 'ert)
(require 'seq)
(eval-and-compile
  (require 'remote-display-generators
           (expand-file-name
            "remote-display-resources/remote-display-generators"
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

(provide 'remoteterm-tests)
;;; remoteterm-tests.el ends here
