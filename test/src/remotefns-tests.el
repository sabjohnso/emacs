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
;;    Decoding a concatenation of messages one at a time gives all
;;    the messages, in order.
;; 3. Incomplete input: decoding a proper prefix of an encoding gives
;;    nil, because the rest of the message has not arrived.
;; 4. Total decoder: for any bytes, decoding gives nil, a message, or
;;    a `remote-display-protocol-error'.  The bytes come from the
;;    network, so the decoder must not trust them.
;;
;; The message types:
;;
;; (hello :version V)
;;     The first message of each side of a connection.
;; (draw-glyphs :frame F :x X :y Y :glyphs [CODE...])
;;     Draw the glyphs with the codes CODE of the font of the frame at
;;     the pixel position X, Y of the baseline.  The number of glyphs
;;     is at most `remotefns-tests--max-glyphs'.
;; (clear-area :frame F :x X :y Y :width W :height H)
;;     Fill the rectangle of the frame at the pixel position X, Y with
;;     the size W, H with the background color of the frame.
;; (copy-area :frame F :x X :y Y :width W :height H :to-x TX :to-y TY)
;;     Copy the rectangle of the frame at X, Y with the size W, H to
;;     the position TX, TY of the same frame.

;;; Code:

(require 'cl-lib)
(require 'ert)
(eval-and-compile
  (require 'remote-display-generators
           (expand-file-name
            "remote-display-resources/remote-display-generators"
            (file-name-directory (macroexp-file-name)))))

(defconst remotefns-tests--max-glyphs 512
  "The maximum number of glyphs in a `draw-glyphs' message.")

(defconst remotefns-tests--types '(hello draw-glyphs clear-area copy-area)
  "The message types.")

(defun remotefns-tests--message ()
  "Return a random valid message."
  (funcall (rd-gen-element (list #'remotefns-tests--hello
                                 #'remotefns-tests--draw-glyphs
                                 #'remotefns-tests--clear-area
                                 #'remotefns-tests--copy-area))))

(defun remotefns-tests--hello ()
  "Return a random `hello' message."
  (list 'hello :version (rd-gen-uint16)))

(defun remotefns-tests--draw-glyphs ()
  "Return a random `draw-glyphs' message."
  (list 'draw-glyphs
        :frame (rd-gen-uint32)
        :x (rd-gen-int16)
        :y (rd-gen-int16)
        :glyphs (let ((count (rd-gen-integer
                              0 (rd-gen-element
                                 (list 8 remotefns-tests--max-glyphs)))))
                  (vconcat (cl-loop repeat count collect (rd-gen-uint32))))))

(defun remotefns-tests--clear-area ()
  "Return a random `clear-area' message."
  (list 'clear-area
        :frame (rd-gen-uint32)
        :x (rd-gen-int16)
        :y (rd-gen-int16)
        :width (rd-gen-uint16)
        :height (rd-gen-uint16)))

(defun remotefns-tests--copy-area ()
  "Return a random `copy-area' message."
  (list 'copy-area
        :frame (rd-gen-uint32)
        :x (rd-gen-int16)
        :y (rd-gen-int16)
        :width (rd-gen-uint16)
        :height (rd-gen-uint16)
        :to-x (rd-gen-int16)
        :to-y (rd-gen-int16)))

(ert-deftest remotefns-tests-round-trip ()
  (skip-unless (featurep 'remote-display))
  (rd-gen-for-all
   #'remotefns-tests--message
   (lambda (message)
     (let ((bytes (remote-display--encode message)))
       (should (equal (remote-display--decode bytes)
                      (cons message (length bytes))))))))

(ert-deftest remotefns-tests-stream ()
  (skip-unless (featurep 'remote-display))
  (rd-gen-for-all
   (lambda () (cons (remotefns-tests--message) (remotefns-tests--message)))
   (lambda (pair)
     (let ((first (remote-display--encode (car pair)))
           (second (remote-display--encode (cdr pair))))
       (should (equal (remote-display--decode (concat first second))
                      (cons (car pair) (length first))))))))

(defun remotefns-tests--decode-all (bytes)
  "Decode the messages in BYTES one at a time, and return their list."
  (let ((messages nil)
        (start 0))
    (while (< start (length bytes))
      (let ((result (remote-display--decode (substring bytes start))))
        (push (car result) messages)
        (setq start (+ start (cdr result)))))
    (nreverse messages)))

(ert-deftest remotefns-tests-stream-of-many ()
  (skip-unless (featurep 'remote-display))
  (rd-gen-for-all
   (lambda () (rd-gen-list #'remotefns-tests--message 5))
   (lambda (messages)
     (should (equal (remotefns-tests--decode-all
                     (apply #'concat (mapcar #'remote-display--encode
                                             messages)))
                    messages)))))

(ert-deftest remotefns-tests-incomplete ()
  (skip-unless (featurep 'remote-display))
  (rd-gen-for-all
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

(ert-deftest remotefns-tests-draw-glyphs-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 19, type 2, frame 1, x -1, y 2, 2 glyphs, the glyph codes.
  (should (equal (remote-display--encode
                  '(draw-glyphs :frame 1 :x -1 :y 2 :glyphs [65 #x01020304]))
                 (unibyte-string 0 0 0 19 2 0 0 0 1 #xff #xff 0 2 0 2
                                 0 0 0 65 1 2 3 4))))

(ert-deftest remotefns-tests-clear-area-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 13, type 3, frame 1, x -1, y 2, width 3, height #x0405.
  (should (equal (remote-display--encode
                  '(clear-area :frame 1 :x -1 :y 2 :width 3 :height #x0405))
                 (unibyte-string 0 0 0 13 3 0 0 0 1 #xff #xff 0 2 0 3
                                 4 5))))

(ert-deftest remotefns-tests-copy-area-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 17, type 4, frame 1, x -1, y 2, width 3, height 4,
  ;; to-x 5, to-y -2.
  (should (equal (remote-display--encode
                  '(copy-area :frame 1 :x -1 :y 2 :width 3 :height 4
                              :to-x 5 :to-y -2))
                 (unibyte-string 0 0 0 17 4 0 0 0 1 #xff #xff 0 2 0 3
                                 0 4 0 5 #xff #xfe))))

(ert-deftest remotefns-tests-too-many-glyphs ()
  (skip-unless (featurep 'remote-display))
  (should-error (remote-display--encode
                 `(draw-glyphs :frame 0 :x 0 :y 0
                               :glyphs ,(make-vector
                                         (1+ remotefns-tests--max-glyphs) 0)))
                :type 'args-out-of-range)
  ;; A frame with 513 glyphs.
  (let ((count (1+ remotefns-tests--max-glyphs)))
    (should (equal (remotefns-tests--decode-error
                    (concat (unibyte-string 0 0 (ash (+ 11 (* 4 count)) -8)
                                            (logand (+ 11 (* 4 count)) #xff)
                                            2 0 0 0 0 0 0 0 0
                                            (ash count -8) (logand count #xff))
                            (make-string (* 4 count) 0)))
                   '(bad-length)))))

(ert-deftest remotefns-tests-glyph-out-of-range ()
  (skip-unless (featurep 'remote-display))
  (should-error (remote-display--encode
                 '(draw-glyphs :frame 0 :x 0 :y 0 :glyphs [#x100000000]))
                :type 'args-out-of-range)
  (should-error (remote-display--encode
                 '(draw-glyphs :frame 0 :x #x8000 :y 0 :glyphs []))
                :type 'args-out-of-range)
  (should-error (remote-display--encode
                 '(clear-area :frame 0 :x 0 :y 0 :width -1 :height 0))
                :type 'args-out-of-range)
  (should-error (remote-display--encode
                 '(copy-area :frame 0 :x 0 :y 0 :width 0 :height 0
                             :to-x 0 :to-y #x8000))
                :type 'args-out-of-range))

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
                 '(bad-length)))
  ;; A clear-area frame without the height.
  (should (equal (remotefns-tests--decode-error
                  (unibyte-string 0 0 0 11 3 0 0 0 1 0 0 0 0 0 3))
                 '(bad-length)))
  ;; A copy-area frame without the to-y field.
  (should (equal (remotefns-tests--decode-error
                  (unibyte-string 0 0 0 15 4 0 0 0 1 0 0 0 0 0 3 0 4 0 5))
                 '(bad-length)))
  ;; A draw-glyphs frame with 2 glyphs and the bytes of only 1.
  (should (equal (remotefns-tests--decode-error
                  (unibyte-string 0 0 0 15 2 0 0 0 1 0 0 0 0 0 2 0 0 0 65))
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
  (if (zerop (rd-gen-random 2))
      (apply #'unibyte-string
             (cl-loop repeat (rd-gen-random 17)
                      collect (rd-gen-random 256)))
    (remotefns-tests--mutate
     (remote-display--encode (remotefns-tests--message)))))

(defun remotefns-tests--mutate (bytes)
  "Return BYTES with one random change."
  (let ((bytes (copy-sequence bytes))
        (index (rd-gen-random (length bytes))))
    (pcase (rd-gen-random 4)
      (0 (aset bytes index (rd-gen-random 256)) bytes)
      (1 (substring bytes 0 index))
      (2 (aset bytes 3 (mod (+ (aref bytes 3) (rd-gen-random 3) -1)
                            256))
         bytes)
      (_ (concat bytes (unibyte-string (rd-gen-random 256)))))))

(ert-deftest remotefns-tests-decode-arbitrary-bytes ()
  "The decoder must accept any input without a crash."
  (skip-unless (featurep 'remote-display))
  (rd-gen-for-all
   #'remotefns-tests--bytes
   (lambda (bytes)
     (let ((result (condition-case nil
                       (remote-display--decode bytes)
                     (remote-display-protocol-error 'rejected))))
       (should (or (null result)
                   (eq result 'rejected)
                   (and (memq (car-safe (car result)) remotefns-tests--types)
                        (<= 5 (cdr result) (length bytes)))))))))

(provide 'remotefns-tests)
;;; remotefns-tests.el ends here
