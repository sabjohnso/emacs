;;; remotefns-tests.el --- tests for src/remotefns.c  -*- lexical-binding:t -*-

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

;; Tests for the message codec of the remote display protocol.  The
;; laws of the codec:
;;
;; 1. Round trip: decoding the encoding of a message gives the same
;;    message, and uses all bytes of the encoding.
;; 2. Stream: decoding the concatenation of two encodings gives the
;;    first message, and uses only the bytes of the first encoding.
;; 3. Incomplete input: decoding a proper prefix of an encoding gives
;;    nil, because the rest of the message has not arrived.
;; 4. Total decoder: for any bytes, decoding gives nil, a message, or
;;    a `remote-display-protocol-error'.  The bytes come from the
;;    network, so the decoder must not trust them.

;;; Code:

(require 'cl-lib)
(require 'ert)

(defconst remotefns-tests--trials 500
  "Number of generated values for each property test.")

(defvar remotefns-tests--random-state nil
  "The random state of the generators.
The tests use their own state, so that they do not change the
random state of other tests.")

(defun remotefns-tests--random (limit)
  "Return a random integer from 0 to LIMIT - 1."
  (cl-random limit remotefns-tests--random-state))

(defun remotefns-tests--for-all (generate check)
  "Call CHECK with values from GENERATE.
Use a fixed seed, so that a failure is reproducible.  The trial
and the value that failed are in the ERT message."
  (setq remotefns-tests--random-state (cl-make-random-state 1))
  (dotimes (trial remotefns-tests--trials)
    (let ((value (funcall generate)))
      (ert-info ((format "%d: %S" trial value) :prefix "Trial: ")
        (funcall check value)))))

(defun remotefns-tests--uint16 ()
  "Return a random 16-bit unsigned integer, often a boundary value."
  (pcase (remotefns-tests--random 4)
    (0 0)
    (1 #xffff)
    (_ (remotefns-tests--random #x10000))))

(defun remotefns-tests--message ()
  "Return a random valid message."
  (list 'hello :version (remotefns-tests--uint16)))

(ert-deftest remotefns-tests-round-trip ()
  (skip-unless (featurep 'remote-display))
  (remotefns-tests--for-all
   #'remotefns-tests--message
   (lambda (message)
     (let ((bytes (remote-display--encode message)))
       (should (equal (remote-display--decode bytes)
                      (cons message (length bytes))))))))

(ert-deftest remotefns-tests-stream ()
  (skip-unless (featurep 'remote-display))
  (remotefns-tests--for-all
   (lambda () (cons (remotefns-tests--message) (remotefns-tests--message)))
   (lambda (pair)
     (let ((first (remote-display--encode (car pair)))
           (second (remote-display--encode (cdr pair))))
       (should (equal (remote-display--decode (concat first second))
                      (cons (car pair) (length first))))))))

(ert-deftest remotefns-tests-incomplete ()
  (skip-unless (featurep 'remote-display))
  (remotefns-tests--for-all
   #'remotefns-tests--message
   (lambda (message)
     (let ((bytes (remote-display--encode message)))
       (dotimes (end (length bytes))
         (should-not (remote-display--decode (substring bytes 0 end))))))))

(ert-deftest remotefns-tests-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 3 (big-endian), type 1, version #x0102 (big-endian).
  (should (equal (remote-display--encode '(hello :version #x0102))
                 (unibyte-string 0 0 0 3 1 1 2))))

(ert-deftest remotefns-tests-encode-is-unibyte ()
  (skip-unless (featurep 'remote-display))
  (should-not (multibyte-string-p
               (remote-display--encode '(hello :version 1)))))

(ert-deftest remotefns-tests-version-out-of-range ()
  (skip-unless (featurep 'remote-display))
  (should-error (remote-display--encode '(hello :version -1))
                :type 'args-out-of-range)
  (should-error (remote-display--encode '(hello :version #x10000))
                :type 'args-out-of-range))

(ert-deftest remotefns-tests-unknown-message ()
  (skip-unless (featurep 'remote-display))
  (should-error (remote-display--encode '(no-such-message))
                :type 'remote-display-protocol-error))

(defun remotefns-tests--decode-error (bytes)
  "Return the data of the protocol error that decoding BYTES signals."
  (cdr (should-error (remote-display--decode bytes)
                     :type 'remote-display-protocol-error)))

(ert-deftest remotefns-tests-unknown-type-byte ()
  (skip-unless (featurep 'remote-display))
  ;; A complete frame: length 1, then type byte 255, which no message
  ;; uses.
  (should (equal (remotefns-tests--decode-error (unibyte-string 0 0 0 1 255))
                 '(unknown-type))))

(ert-deftest remotefns-tests-bad-length ()
  (skip-unless (featurep 'remote-display))
  ;; Length 0: no type byte.
  (should (equal (remotefns-tests--decode-error (unibyte-string 0 0 0 0))
                 '(bad-length)))
  ;; A hello frame with a body that is too short.
  (should (equal (remotefns-tests--decode-error (unibyte-string 0 0 0 2 1 0))
                 '(bad-length))))

(ert-deftest remotefns-tests-frame-too-long ()
  (skip-unless (featurep 'remote-display))
  ;; Reject the length at once.  Do not wait for 4 GiB of input.
  (should (equal (remotefns-tests--decode-error
                  (unibyte-string #xff #xff #xff #xff 1))
                 '(frame-too-long))))

(ert-deftest remotefns-tests-early-rejection ()
  (skip-unless (featurep 'remote-display))
  ;; Reject a bad header as soon as the type byte arrives.  Do not
  ;; wait for the rest of the frame.
  (should (equal (remotefns-tests--decode-error (unibyte-string 0 0 0 9 255))
                 '(unknown-type)))
  (should (equal (remotefns-tests--decode-error (unibyte-string 0 0 0 2 1))
                 '(bad-length))))

(ert-deftest remotefns-tests-extension ()
  (skip-unless (featurep 'remote-display))
  ;; A newer peer can append fields to a message.  An older peer
  ;; ignores them, and uses the whole frame.
  (should (equal (remote-display--decode (unibyte-string 0 0 0 5 1 0 7 9 9))
                 '((hello :version 7) . 9))))

(ert-deftest remotefns-tests-decode-multibyte ()
  (skip-unless (featurep 'remote-display))
  (should-error (remote-display--decode
                 (string-to-multibyte (unibyte-string 0 0 0 3 1 0 1)))
                :type 'error))

(defun remotefns-tests--bytes ()
  "Return a random unibyte string for the decoder.
Half of the strings are random bytes.  The other half are valid
frames with one change, so that the decoder reaches the body."
  (if (zerop (remotefns-tests--random 2))
      (apply #'unibyte-string
             (cl-loop repeat (remotefns-tests--random 17)
                      collect (remotefns-tests--random 256)))
    (remotefns-tests--mutate
     (remote-display--encode (remotefns-tests--message)))))

(defun remotefns-tests--mutate (bytes)
  "Return BYTES with one random change."
  (let ((bytes (copy-sequence bytes))
        (index (remotefns-tests--random (length bytes))))
    (pcase (remotefns-tests--random 4)
      (0 (aset bytes index (remotefns-tests--random 256)) bytes)
      (1 (substring bytes 0 index))
      (2 (aset bytes 3 (mod (+ (aref bytes 3) (remotefns-tests--random 3) -1)
                            256))
         bytes)
      (_ (concat bytes (unibyte-string (remotefns-tests--random 256)))))))

(ert-deftest remotefns-tests-decode-arbitrary-bytes ()
  "The decoder must accept any input without a crash."
  (skip-unless (featurep 'remote-display))
  (remotefns-tests--for-all
   #'remotefns-tests--bytes
   (lambda (bytes)
     (let ((result (condition-case nil
                       (remote-display--decode bytes)
                     (remote-display-protocol-error 'rejected))))
       (should (or (null result)
                   (eq result 'rejected)
                   (and (eq (car-safe (car result)) 'hello)
                        (<= 5 (cdr result) (length bytes)))))))))

(provide 'remotefns-tests)
;;; remotefns-tests.el ends here
