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
;; and the tests do not change the random state of other tests.

;;; Code:

(require 'cl-lib)
(require 'ert)

(defconst rd-gen-trials 500
  "Number of generated values for each property test.")

(defvar rd-gen--random-state nil
  "The random state of the generators.")

(defun rd-gen-for-all (generate check)
  "Call CHECK with `rd-gen-trials' values from GENERATE.
Use a fixed seed.  The trial and the value that failed are in the
ERT message."
  (setq rd-gen--random-state (cl-make-random-state 1))
  (dotimes (trial rd-gen-trials)
    (let ((value (funcall generate)))
      (ert-info ((format "%d: %S" trial value) :prefix "Trial: ")
        (funcall check value)))))

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

(provide 'remote-display-generators)
;;; remote-display-generators.el ends here
