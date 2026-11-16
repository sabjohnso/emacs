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
;;    the messages, in order.  Decoding from a start position gives
;;    what decoding the rest of the bytes gives.
;; 3. Incomplete input: decoding a proper prefix of an encoding gives
;;    nil, because the rest of the message has not arrived.
;; 4. Total decoder: for any bytes, decoding gives nil, a message, or
;;    a `remote-display-protocol-error'.  The bytes come from the
;;    network, so the decoder must not trust them.
;; 4a. Skip: an unknown type byte is an error, and the error data gives
;;    the size of the frame.  A receiver skips that number of bytes,
;;    and the next message decodes.  So a newer peer can send a new
;;    message type to an older peer.  The type bytes start at 1, and
;;    the end of the table of message types is the end of the valid
;;    bytes.
;; 5. Limits: a message with `remote-display--max-glyphs' glyphs or
;;    `remote-display--max-fringe-rows' rows has a round trip, and one
;;    more is an error.  Lisp reads these limits from the codec, and
;;    cannot change them.
;; 6. Formats: `remote-display--message-formats' gives each message
;;    type and its fields, in the order of the wire.  Each type has a
;;    generator here, and a generator gives the fields of its type.
;;    So a new type or a new field fails until these tests know it.
;;
;; The message types come in blocks, in the order of the blocks of
;; RD_MESSAGES in src/rdproto.h.  A new type of a block changes the
;; type bytes of all later blocks.  Thus no test uses a literal type
;; byte for a message after the base block.  A wire-format example
;; computes the type byte from the position of the type in
;; `remote-display--message-formats'.
;;
;; The message types:
;;
;; (hello :version V)
;;     The first message of each side of a connection.
;; (draw-glyphs :frame F :x X :y Y :clip-x CX :clip-y CY :clip-width CW
;;              :clip-height CH :color C :font ID :glyphs [CODE...])
;;     Draw the glyphs with the codes CODE of the font ID at the pixel
;;     position X, Y of the baseline, in the color C.  The
;;     client changes only the pixels in the rectangle at CX, CY with
;;     the size CW, CH.
;;     The number of glyphs is at most `remote-display--max-glyphs'.
;; (clear-area :frame F :x X :y Y :width W :height H :color C)
;;     Fill the rectangle of the frame at the pixel position X, Y with
;;     the size W, H with the color C.
;; (copy-area :frame F :x X :y Y :width W :height H :to-x TX :to-y TY)
;;     Copy the rectangle of the frame at X, Y with the size W, H to
;;     the position TX, TY of the same frame.
;; (draw-cursor :frame F :x X :y Y :width W :height H :kind K :color C)
;;     Draw the cursor of kind K in the color C in the rectangle at X,
;;     Y with the size W, H.  The kinds are 0 for a filled box, 1 for a hollow
;;     box, 2 for a bar, and 3 for a horizontal bar.  A client ignores
;;     a kind that it does not know.
;; (draw-fringe :frame F :x X :y Y :width W :overlay O :cursor C
;;              :foreground FG :background BG :rows [BITS...])
;;     Draw a fringe bitmap of W pixels width at X, Y.  Each element
;;     of the vector is one row of pixels.  A bit that is 1 has the
;;     color FG, and a bit that is 0 has the color BG.  If O is not 0,
;;     draw only the bits that are 1, on the pixels that are there.
;;     If C is not 0, the bitmap is the cursor in the fringe.  The
;;     number of rows is at most `remote-display--max-fringe-rows'.
;; (define-font :font ID :index I :size S :hash [BYTE...] :file-size N)
;;     The font ID of the session is the face I of the font file with
;;     the SHA-256 hash BYTE..., at the size of S pixels.  The hash has
;;     `remote-display--hash-size' bytes.  The file has N bytes.
;; (font-data :hash [BYTE...] :offset O :total N :bytes [BYTE...])
;;     The bytes of the font file with the hash, from the offset O.  The
;;     file has N bytes.  A message has at most
;;     `remote-display--max-font-chunk' bytes of the file.
;; (define-frame :frame F :width W :height H)
;;     The frame F exists, and it has the size W, H in pixels.  A
;;     client opens a window for a new frame, and changes the size of
;;     the window of a frame that it knows.
;; (delete-frame :frame F)
;;     The frame F does not exist now.  A client closes its window.
;;
;; A client sends these messages to the server:
;;
;; (key :frame F :time T :keysym K :character C :modifiers M)
;;     The user pressed the key K, an X keysym, in the window of the
;;     frame F.  C is the Unicode character of the key, or 0.  M has the
;;     bits of `remote-display--modifier-bits'.  T is the time of the
;;     client in milliseconds, which can wrap.
;; (button :frame F :time T :x X :y Y :button B :modifiers M :press P)
;;     The user pressed (P is 1) or released (P is 0) the mouse button
;;     B at X, Y.
;; (motion :frame F :time T :x X :y Y)
;;     The mouse moved to X, Y.
;; (focus :frame F :in I)
;;     The window of F got (I is 1) or lost (I is 0) the focus.
;; (resize :frame F :width W :height H)
;;     The window of F has the size W, H in pixels now.
;; (close-frame :frame F)
;;     The user asked to close the window of F.  The server decides
;;     whether to delete F.
;; (want-font :hash [BYTE...])
;;     The client does not have the font file with the hash, and asks
;;     the server for it.
;;
;; Both sides send this message:
;;
;; (clipboard-data :serial S :offset O :total N :bytes [BYTE...])
;;     The bytes of the UTF-8 text S of the clipboard, from the offset O.
;;     The text has N bytes.  A message has at most
;;     `remote-display--max-clipboard-chunk' bytes of the text.
;;
;; The server sends these messages of images (the image block):
;;
;; (draw-image :frame F :image ID :x X :y Y :clip-x CX :clip-y CY
;;             :clip-width CW :clip-height CH)
;;     Paint the pixels of the image ID with its top-left pixel at X, Y
;;     of the frame F, over the pixels that are there.  The client
;;     changes only the pixels in the rectangle at CX, CY with the size
;;     CW, CH.  An image that the client does not have changes no pixel.
;; (image-data :image ID :offset O :total N :bytes [BYTE...])
;;     The bytes of the PNG file of the image ID, from the offset O.
;;     The file has N bytes.  A message has at most
;;     `remote-display--max-image-chunk' bytes of the file.
;; (forget-image :image ID)
;;     The server does not use the image ID again.  The client frees
;;     it.
;;
;; Each X, Y, CX, CY, TX, and TY is a signed 32-bit integer.  Each W, H,
;; CW, and CH is an unsigned 32-bit integer, but W of `draw-fringe' has
;; 16 bits, because a row of a fringe bitmap has 16 bits.  Each color
;; is an unsigned 32-bit integer #xRRGGBB.  The server sets the high 8
;; bits to 0, and a client ignores them.

;;; Code:

(require 'cl-lib)
(require 'ert)
(eval-and-compile
  (require 'remote-display-generators
           (expand-file-name
            "remote-display-resources/remote-display-generators"
            (file-name-directory (macroexp-file-name)))))

(defvar remote-display--max-glyphs)
(defvar remote-display--hash-size)
(defvar remote-display--max-font-chunk)
(defvar remote-display--max-clipboard-chunk)
(defvar remote-display--max-image-chunk)
(defvar remote-display--max-fringe-rows)
(defvar remote-display--message-formats)
(defvar remote-display--modifier-bits)

;; A build without the remote window system does not have these
;; functions.  The tests of such a build are skipped.
(declare-function remote-display--decode "remotefns.c" (bytes &optional start))
(declare-function remote-display--encode "remotefns.c" (message))

(defconst remotefns-tests--base-generators
  '((hello . remotefns-tests--hello)
    (draw-glyphs . remotefns-tests--draw-glyphs)
    (clear-area . remotefns-tests--clear-area)
    (copy-area . remotefns-tests--copy-area)
    (draw-cursor . remotefns-tests--draw-cursor)
    (draw-fringe . remotefns-tests--draw-fringe)
    (define-font . remotefns-tests--define-font)
    (define-frame . remotefns-tests--define-frame)
    (delete-frame . remotefns-tests--delete-frame)
    (key . remotefns-tests--key)
    (button . remotefns-tests--button)
    (motion . remotefns-tests--motion)
    (focus . remotefns-tests--focus)
    (resize . remotefns-tests--resize)
    (ping . remotefns-tests--ping)
    (pong . remotefns-tests--pong)
    (close-frame . remotefns-tests--close-frame)
    (want-font . remotefns-tests--want-font)
    (font-data . remotefns-tests--font-data)
    (clipboard-data . remotefns-tests--clipboard-data))
  "The message types of the base block, each with its generator.")

(defconst remotefns-tests--image-generators
  '((draw-image . remotefns-tests--draw-image)
    (image-data . remotefns-tests--image-data)
    (forget-image . remotefns-tests--forget-image))
  "The message types of the image block, each with its generator.
The block is for the feature Images (PLAN.org feature-images).")

(defconst remotefns-tests--child-frame-generators
  '((place-frame . remotefns-tests--place-frame))
  "The message types of the child frame block, each with its generator.
The block is for the feature Tooltips and child frames (PLAN.org
feature-child-frames).")

(defconst remotefns-tests--menu-generators nil
  "The message types of the menu block, each with its generator.
The block is for the feature Menus drawn by Emacs (PLAN.org
feature-menus).")

(defconst remotefns-tests--generators
  (append remotefns-tests--base-generators
          remotefns-tests--image-generators
          remotefns-tests--child-frame-generators
          remotefns-tests--menu-generators)
  "The message types, each with the function that generates a message.
The blocks come in the order of the blocks of RD_MESSAGES in
src/rdproto.h.")

(defconst remotefns-tests--clip-and-color
  '(:clip-x 0 :clip-y 0 :clip-width 0 :clip-height 0 :color 0 :font 0)
  "The fields of a `draw-glyphs' message between the position and the glyphs.")

(defun remotefns-tests--message ()
  "Return a random valid message."
  (funcall (cdr (rd-gen-element remotefns-tests--generators))))

(defun remotefns-tests--hello ()
  "Return a random `hello' message."
  (list 'hello :version (rd-gen-uint16)))

(defun remotefns-tests--draw-glyphs ()
  "Return a random `draw-glyphs' message."
  (list 'draw-glyphs
        :frame (rd-gen-uint32)
        :x (rd-gen-int32)
        :y (rd-gen-int32)
        :clip-x (rd-gen-int32)
        :clip-y (rd-gen-int32)
        :clip-width (rd-gen-uint32)
        :clip-height (rd-gen-uint32)
        :color (rd-gen-uint32)
        :font (rd-gen-uint32)
        :glyphs (let ((count (rd-gen-integer
                              0 (rd-gen-element
                                 (list 8 remote-display--max-glyphs)))))
                  (vconcat (cl-loop repeat count collect (rd-gen-uint32))))))

(defun remotefns-tests--clear-area ()
  "Return a random `clear-area' message."
  (list 'clear-area
        :frame (rd-gen-uint32)
        :x (rd-gen-int32)
        :y (rd-gen-int32)
        :width (rd-gen-uint32)
        :height (rd-gen-uint32)
        :color (rd-gen-uint32)))

(defun remotefns-tests--copy-area ()
  "Return a random `copy-area' message."
  (list 'copy-area
        :frame (rd-gen-uint32)
        :x (rd-gen-int32)
        :y (rd-gen-int32)
        :width (rd-gen-uint32)
        :height (rd-gen-uint32)
        :to-x (rd-gen-int32)
        :to-y (rd-gen-int32)))

(defun remotefns-tests--draw-cursor ()
  "Return a random `draw-cursor' message."
  (list 'draw-cursor
        :frame (rd-gen-uint32)
        :x (rd-gen-int32)
        :y (rd-gen-int32)
        :width (rd-gen-uint32)
        :height (rd-gen-uint32)
        :kind (rd-gen-integer 0 255)
        :color (rd-gen-uint32)))

(defun remotefns-tests--draw-fringe ()
  "Return a random `draw-fringe' message."
  (list 'draw-fringe
        :frame (rd-gen-uint32)
        :x (rd-gen-int32)
        :y (rd-gen-int32)
        :width (rd-gen-uint16)
        :overlay (rd-gen-integer 0 255)
        :cursor (rd-gen-integer 0 255)
        :foreground (rd-gen-uint32)
        :background (rd-gen-uint32)
        :rows (let ((count (rd-gen-element
                            (list 0 1 remote-display--max-fringe-rows
                                  (rd-gen-integer
                                   0 remote-display--max-fringe-rows)))))
                (vconcat (cl-loop repeat count collect (rd-gen-uint16))))))

(defun remotefns-tests--define-font ()
  "Return a random `define-font' message."
  (list 'define-font
        :font (rd-gen-uint32)
        :index (rd-gen-uint32)
        :size (rd-gen-uint32)
        :hash (remotefns-tests--hash)
        :file-size (rd-gen-uint32)))

(defun remotefns-tests--hash ()
  "Return a random hash of at most `remote-display--hash-size' bytes."
  (remotefns-tests--byte-vector remote-display--hash-size))

(defun remotefns-tests--byte-vector (limit)
  "Return a vector of at most LIMIT random bytes."
  (vconcat (cl-loop repeat (rd-gen-integer 0 limit)
                    collect (rd-gen-integer 0 255))))

(defun remotefns-tests--define-frame ()
  "Return a random `define-frame' message."
  (list 'define-frame
        :frame (rd-gen-uint32)
        :width (rd-gen-uint32)
        :height (rd-gen-uint32)))

(defun remotefns-tests--delete-frame ()
  "Return a random `delete-frame' message."
  (list 'delete-frame :frame (rd-gen-uint32)))

(defun remotefns-tests--key ()
  "Return a random `key' message."
  (list 'key :frame (rd-gen-uint32) :time (rd-gen-uint32)
        :keysym (rd-gen-uint32) :character (rd-gen-uint32)
        :modifiers (rd-gen-integer 0 255)))

(defun remotefns-tests--button ()
  "Return a random `button' message."
  (list 'button :frame (rd-gen-uint32) :time (rd-gen-uint32)
        :x (rd-gen-int32) :y (rd-gen-int32)
        :button (rd-gen-integer 0 255) :modifiers (rd-gen-integer 0 255)
        :press (rd-gen-integer 0 255)))

(defun remotefns-tests--motion ()
  "Return a random `motion' message."
  (list 'motion :frame (rd-gen-uint32) :time (rd-gen-uint32)
        :x (rd-gen-int32) :y (rd-gen-int32)))

(defun remotefns-tests--focus ()
  "Return a random `focus' message."
  (list 'focus :frame (rd-gen-uint32) :in (rd-gen-integer 0 255)))

(defun remotefns-tests--resize ()
  "Return a random `resize' message."
  (list 'resize :frame (rd-gen-uint32) :width (rd-gen-uint32)
        :height (rd-gen-uint32)))

(defun remotefns-tests--ping ()
  "Return a random `ping' message."
  (list 'ping :serial (rd-gen-uint32)))

(defun remotefns-tests--pong ()
  "Return a random `pong' message."
  (list 'pong :serial (rd-gen-uint32)))

(defun remotefns-tests--close-frame ()
  "Return a random `close-frame' message."
  (list 'close-frame :frame (rd-gen-uint32)))

(defun remotefns-tests--want-font ()
  "Return a random `want-font' message."
  (list 'want-font :hash (remotefns-tests--hash)))

(defun remotefns-tests--font-data ()
  "Return a random `font-data' message."
  (list 'font-data :hash (remotefns-tests--hash)
        :offset (rd-gen-uint32) :total (rd-gen-uint32)
        :bytes (remotefns-tests--byte-vector
                remote-display--max-font-chunk)))

(defun remotefns-tests--clipboard-data ()
  "Return a random `clipboard-data' message."
  (list 'clipboard-data :serial (rd-gen-uint32)
        :offset (rd-gen-uint32) :total (rd-gen-uint32)
        :bytes (remotefns-tests--byte-vector
                remote-display--max-clipboard-chunk)))

(defun remotefns-tests--draw-image ()
  "Return a random `draw-image' message."
  (list 'draw-image
        :frame (rd-gen-uint32)
        :image (rd-gen-uint32)
        :x (rd-gen-int32)
        :y (rd-gen-int32)
        :clip-x (rd-gen-int32)
        :clip-y (rd-gen-int32)
        :clip-width (rd-gen-uint32)
        :clip-height (rd-gen-uint32)))

(defun remotefns-tests--image-data ()
  "Return a random `image-data' message."
  (list 'image-data :image (rd-gen-uint32)
        :offset (rd-gen-uint32) :total (rd-gen-uint32)
        :bytes (remotefns-tests--byte-vector
                remote-display--max-image-chunk)))

(defun remotefns-tests--forget-image ()
  "Return a random `forget-image' message."
  (list 'forget-image :image (rd-gen-uint32)))

(defun remotefns-tests--place-frame ()
  "Return a random `place-frame' message."
  (list 'place-frame :frame (rd-gen-uint32) :parent (rd-gen-uint32)
        :x (rd-gen-int32) :y (rd-gen-int32) :z (rd-gen-uint32)
        :visible (rd-gen-integer 0 255)))

(ert-deftest remotefns-tests-each-type-has-generator ()
  "Each message type of the codec has a generator."
  (skip-unless (featurep 'remote-display))
  (should (equal (mapcar #'car remote-display--message-formats)
                 (mapcar #'car remotefns-tests--generators))))

(ert-deftest remotefns-tests-generators-give-all-fields ()
  "A generator gives the fields of its message type, in order."
  (skip-unless (featurep 'remote-display))
  (rd-gen-for-all
   #'remotefns-tests--message
   (lambda (message)
     (should (equal (cons (car message)
                          (cl-loop for (key) on (cdr message) by #'cddr
                                   collect key))
                    (assq (car message) remote-display--message-formats))))))

(ert-deftest remotefns-tests-formats-are-private ()
  "A change of the Lisp list does not change the codec."
  (skip-unless (featurep 'remote-display))
  (should-error (set 'remote-display--message-formats nil)
                :type 'setting-constant)
  (let ((format (assq 'hello remote-display--message-formats)))
    (setcar (cdr format) :changed)
    (unwind-protect
        (should (equal (remote-display--encode '(hello :version 1))
                       (unibyte-string 0 0 0 3 1 0 1)))
      (setcar (cdr format) :version))))

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

(ert-deftest remotefns-tests-decode-from-start ()
  "Decoding from a start position ignores the bytes before it."
  (skip-unless (featurep 'remote-display))
  (rd-gen-for-all
   (lambda () (cons (rd-gen-bytes 20) (remotefns-tests--message)))
   (lambda (case)
     (let* ((before (car case))
            (bytes (remote-display--encode (cdr case)))
            (start (length before)))
       (should (equal (remote-display--decode (concat before bytes) start)
                      (cons (cdr case) (length bytes))))
       (should-not (remote-display--decode (concat before bytes)
                                           (+ start (length bytes))))
       (should-not (remote-display--decode
                    (concat before (substring bytes 0 1)) start))))))

(ert-deftest remotefns-tests-decode-start-range ()
  "A start position outside BYTES is an error."
  (skip-unless (featurep 'remote-display))
  (dolist (start (list -1 3 most-positive-fixnum))
    (should-error (remote-display--decode "ab" start)
                  :type 'args-out-of-range)))

(ert-deftest remotefns-tests-decode-start-type ()
  "A start position that is not a fixnum is an error."
  (skip-unless (featurep 'remote-display))
  (dolist (start (list "x" 'a 1.0 (1+ most-positive-fixnum)))
    (should-error (remote-display--decode "ab" start)
                  :type 'wrong-type-argument)))

(defun remotefns-tests--decode-all (bytes)
  "Decode the messages in BYTES one at a time, and return their list."
  (let ((messages nil)
        (start 0))
    (while (< start (length bytes))
      (let ((result (remote-display--decode bytes start)))
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
  ;; Length 47, type 2, frame 1, x -1, y 2, clip-x -2, clip-y 3,
  ;; clip-width 4, clip-height 5, color #x0a0b0c, font 6, 2 glyphs, the
  ;; glyph codes.
  (should (equal (remote-display--encode
                  '(draw-glyphs :frame 1 :x -1 :y 2 :clip-x -2 :clip-y 3
                                :clip-width 4 :clip-height 5 :color #x0a0b0c
                                :font 6 :glyphs [65 #x01020304]))
                 (unibyte-string 0 0 0 47 2 0 0 0 1 #xff #xff #xff #xff
                                 0 0 0 2 #xff #xff #xff #xfe 0 0 0 3
                                 0 0 0 4 0 0 0 5 0 #x0a #x0b #x0c 0 0 0 6
                                 0 2 0 0 0 65 1 2 3 4))))

(ert-deftest remotefns-tests-define-font-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 22, type 7, font 1, index 2, size #x0102, 3 hash bytes,
  ;; file size #x01020304.
  (should (equal (remote-display--encode
                  '(define-font :font 1 :index 2 :size #x0102
                                :hash [#xab 0 #xff] :file-size #x01020304))
                 (unibyte-string 0 0 0 22 7 0 0 0 1 0 0 0 2 0 0 1 2
                                 0 3 #xab 0 #xff 1 2 3 4))))

(ert-deftest remotefns-tests-hash-limits ()
  "A hash has at most `remote-display--hash-size' bytes."
  (skip-unless (featurep 'remote-display))
  (should (= remote-display--hash-size 32))
  (should-error (remote-display--encode
                 `(define-font :font 0 :index 0 :size 0
                               :hash ,(make-vector 33 0) :file-size 0))
                :type 'args-out-of-range)
  (should-error (remote-display--encode
                 '(define-font :font 0 :index 0 :size 0 :hash [256]
                               :file-size 0))
                :type 'args-out-of-range)
  ;; A frame that says it has one hash byte too many.
  (should (equal (remotefns-tests--decode-error
                  (concat (unibyte-string 0 0 0 48 7 0 0 0 0 0 0 0 0
                                          0 0 0 0 0 33)
                          (make-string 33 0)))
                 '(bad-length))))

(ert-deftest remotefns-tests-clear-area-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 25, type 3, frame 1, x -1, y 2, width 3, height #x0405,
  ;; color #x0d0e0f.
  (should (equal (remote-display--encode
                  '(clear-area :frame 1 :x -1 :y 2 :width 3 :height #x0405
                               :color #x0d0e0f))
                 (unibyte-string 0 0 0 25 3 0 0 0 1 #xff #xff #xff #xff
                                 0 0 0 2 0 0 0 3 0 0 4 5 0 #x0d #x0e #x0f))))

(ert-deftest remotefns-tests-copy-area-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 29, type 4, frame 1, x -1, y 2, width 3, height 4,
  ;; to-x 5, to-y -2.
  (should (equal (remote-display--encode
                  '(copy-area :frame 1 :x -1 :y 2 :width 3 :height 4
                              :to-x 5 :to-y -2))
                 (unibyte-string 0 0 0 29 4 0 0 0 1 #xff #xff #xff #xff
                                 0 0 0 2 0 0 0 3 0 0 0 4 0 0 0 5
                                 #xff #xff #xff #xfe))))

(ert-deftest remotefns-tests-draw-cursor-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 26, type 5, frame 1, x -1, y 2, width 3, height 4, kind 2,
  ;; color #x102030.
  (should (equal (remote-display--encode
                  '(draw-cursor :frame 1 :x -1 :y 2 :width 3 :height 4
                                :kind 2 :color #x102030))
                 (unibyte-string 0 0 0 26 5 0 0 0 1 #xff #xff #xff #xff
                                 0 0 0 2 0 0 0 3 0 0 0 4 2
                                 0 #x10 #x20 #x30))))

(ert-deftest remotefns-tests-draw-fringe-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 31, type 6, frame 1, x -1, y 2, width 8, overlay 1,
  ;; cursor 0, foreground #x112233, background #x445566, 2 rows, the
  ;; rows.
  (should (equal (remote-display--encode
                  '(draw-fringe :frame 1 :x -1 :y 2 :width 8 :overlay 1
                                :cursor 0 :foreground #x112233
                                :background #x445566 :rows [#x0102 #xff]))
                 (unibyte-string 0 0 0 31 6 0 0 0 1 #xff #xff #xff #xff
                                 0 0 0 2 0 8 1 0 0 #x11 #x22 #x33
                                 0 #x44 #x55 #x66 0 2 1 2 0 #xff))))

(ert-deftest remotefns-tests-define-frame-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 13, type 8, frame 1, width #x0203, height 4.
  (should (equal (remote-display--encode
                  '(define-frame :frame 1 :width #x0203 :height 4))
                 (unibyte-string 0 0 0 13 8 0 0 0 1 0 0 2 3 0 0 0 4))))

(ert-deftest remotefns-tests-delete-frame-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 5, type 9, frame #x01020304.
  (should (equal (remote-display--encode '(delete-frame :frame #x01020304))
                 (unibyte-string 0 0 0 5 9 1 2 3 4))))

(ert-deftest remotefns-tests-key-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 18, type 10, frame 1, time 5, keysym #xff0d, character 0,
  ;; modifiers 2.
  (should (equal (remote-display--encode
                  '(key :frame 1 :time 5 :keysym #xff0d :character 0
                        :modifiers 2))
                 (unibyte-string 0 0 0 18 10 0 0 0 1 0 0 0 5
                                 0 0 #xff #x0d 0 0 0 0 2))))

(ert-deftest remotefns-tests-button-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 20, type 11, frame 1, time 2, x -1, y 3, button 1,
  ;; modifiers 4, press 1.
  (should (equal (remote-display--encode
                  '(button :frame 1 :time 2 :x -1 :y 3 :button 1
                           :modifiers 4 :press 1))
                 (unibyte-string 0 0 0 20 11 0 0 0 1 0 0 0 2
                                 #xff #xff #xff #xff 0 0 0 3 1 4 1))))

(ert-deftest remotefns-tests-motion-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 17, type 12, frame 1, time 2, x 3, y -2.
  (should (equal (remote-display--encode
                  '(motion :frame 1 :time 2 :x 3 :y -2))
                 (unibyte-string 0 0 0 17 12 0 0 0 1 0 0 0 2
                                 0 0 0 3 #xff #xff #xff #xfe))))

(ert-deftest remotefns-tests-focus-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 6, type 13, frame 7, in 1.
  (should (equal (remote-display--encode '(focus :frame 7 :in 1))
                 (unibyte-string 0 0 0 6 13 0 0 0 7 1))))

(ert-deftest remotefns-tests-resize-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 13, type 14, frame 1, width 2, height 3.
  (should (equal (remote-display--encode
                  '(resize :frame 1 :width 2 :height 3))
                 (unibyte-string 0 0 0 13 14 0 0 0 1 0 0 0 2 0 0 0 3))))

(ert-deftest remotefns-tests-ping-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 5, type 15, serial #x01020304.
  (should (equal (remote-display--encode '(ping :serial #x01020304))
                 (unibyte-string 0 0 0 5 15 1 2 3 4))))

(ert-deftest remotefns-tests-pong-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 5, type 16, serial #x01020304.
  (should (equal (remote-display--encode '(pong :serial #x01020304))
                 (unibyte-string 0 0 0 5 16 1 2 3 4))))

(ert-deftest remotefns-tests-close-frame-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 5, type 17, frame 7.
  (should (equal (remote-display--encode '(close-frame :frame 7))
                 (unibyte-string 0 0 0 5 17 0 0 0 7))))

(ert-deftest remotefns-tests-want-font-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 5, type 18, 2 hash bytes.
  (should (equal (remote-display--encode '(want-font :hash [#xab #xcd]))
                 (unibyte-string 0 0 0 5 18 0 2 #xab #xcd))))

(ert-deftest remotefns-tests-font-data-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 16, type 19, 1 hash byte, offset #x0102, total #x0304,
  ;; 2 bytes.
  (should (equal (remote-display--encode
                  '(font-data :hash [#xab] :offset #x0102 :total #x0304
                              :bytes [0 #xff]))
                 (unibyte-string 0 0 0 16 19 0 1 #xab 0 0 1 2 0 0 3 4
                                 0 2 0 #xff))))

(ert-deftest remotefns-tests-clipboard-data-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 17, type 20, serial 7, offset #x0102, total #x0304, 2 bytes.
  (should (equal (remote-display--encode
                  '(clipboard-data :serial 7 :offset #x0102 :total #x0304
                                   :bytes [?a #xff]))
                 (unibyte-string 0 0 0 17 20 0 0 0 7 0 0 1 2 0 0 3 4
                                 0 2 ?a #xff))))

(ert-deftest remotefns-tests-clipboard-chunk-limits ()
  "A clipboard-data message has at most 8192 bytes of text."
  (skip-unless (featurep 'remote-display))
  (should (= remote-display--max-clipboard-chunk 8192))
  (should (remote-display--encode
           `(clipboard-data :serial 0 :offset 0 :total 0
                            :bytes ,(make-vector 8192 0))))
  (should-error (remote-display--encode
                 `(clipboard-data :serial 0 :offset 0 :total 0
                                  :bytes ,(make-vector 8193 0)))
                :type 'args-out-of-range))

(defun remotefns-tests--type-byte (type)
  "Return the type byte of the message type TYPE.
The type bytes start at 1, in the order of
`remote-display--message-formats'."
  (1+ (cl-position type (mapcar #'car remote-display--message-formats))))

(ert-deftest remotefns-tests-draw-image-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 33, the type byte, frame 7, image 9, x -1, y 2, clip-x 3,
  ;; clip-y 4, clip-width 5, clip-height 6.
  (should (equal (remote-display--encode
                  '(draw-image :frame 7 :image 9 :x -1 :y 2 :clip-x 3
                               :clip-y 4 :clip-width 5 :clip-height 6))
                 (unibyte-string 0 0 0 33
                                 (remotefns-tests--type-byte 'draw-image)
                                 0 0 0 7 0 0 0 9 #xff #xff #xff #xff
                                 0 0 0 2 0 0 0 3 0 0 0 4 0 0 0 5
                                 0 0 0 6))))

(ert-deftest remotefns-tests-image-data-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 17, the type byte, image 7, offset #x0102, total #x0304,
  ;; 2 bytes.
  (should (equal (remote-display--encode
                  '(image-data :image 7 :offset #x0102 :total #x0304
                               :bytes [#x89 ?P]))
                 (unibyte-string 0 0 0 17
                                 (remotefns-tests--type-byte 'image-data)
                                 0 0 0 7 0 0 1 2 0 0 3 4 0 2 #x89 ?P))))

(ert-deftest remotefns-tests-forget-image-wire-format ()
  (skip-unless (featurep 'remote-display))
  ;; Length 5, the type byte, image 7.
  (should (equal (remote-display--encode '(forget-image :image 7))
                 (unibyte-string 0 0 0 5
                                 (remotefns-tests--type-byte 'forget-image)
                                 0 0 0 7))))

(ert-deftest remotefns-tests-image-chunk-limits ()
  "An image-data message has at most 8192 bytes of the file."
  (skip-unless (featurep 'remote-display))
  (should (= remote-display--max-image-chunk 8192))
  (should (remote-display--encode
           `(image-data :image 0 :offset 0 :total 0
                        :bytes ,(make-vector 8192 0))))
  (should-error (remote-display--encode
                 `(image-data :image 0 :offset 0 :total 0
                              :bytes ,(make-vector 8193 0)))
                :type 'args-out-of-range))

(ert-deftest remotefns-tests-font-chunk-limits ()
  "A font-data message has at most `remote-display--max-font-chunk' bytes."
  (skip-unless (featurep 'remote-display))
  (should (= remote-display--max-font-chunk 8192))
  (should (remote-display--encode
           `(font-data :hash [] :offset 0 :total 0
                       :bytes ,(make-vector 8192 0))))
  (should-error (remote-display--encode
                 `(font-data :hash [] :offset 0 :total 0
                             :bytes ,(make-vector 8193 0)))
                :type 'args-out-of-range))

(ert-deftest remotefns-tests-modifier-bits ()
  "Each modifier has one bit, and the bits are the same for each build.
The bits are part of the wire format, so a change of them changes the
protocol."
  (skip-unless (featurep 'remote-display))
  (should (equal remote-display--modifier-bits
                 '((shift . 1) (control . 2) (meta . 4) (super . 8)
                   (hyper . 16) (alt . 32)))))

(ert-deftest remotefns-tests-byte-order-of-each-value ()
  "All bytes of a value are in big-endian order, not only the low bytes."
  (skip-unless (featurep 'remote-display))
  ;; Length 25, type 3, frame, x, y, width, height, color.
  (should (equal (remote-display--encode
                  `(clear-area :frame #x01020304 :x #x12345678
                               :y ,(- (ash 1 31)) :width #x05060708
                               :height #xf1f2f3f4 :color #xf5f6f7f8))
                 (unibyte-string 0 0 0 25 3 1 2 3 4 #x12 #x34 #x56 #x78
                                 #x80 0 0 0 5 6 7 8
                                 #xf1 #xf2 #xf3 #xf4 #xf5 #xf6 #xf7 #xf8))))

(ert-deftest remotefns-tests-too-many-fringe-rows ()
  (skip-unless (featurep 'remote-display))
  (should-error (remote-display--encode
                 `(draw-fringe :frame 0 :x 0 :y 0 :width 0 :overlay 0 :cursor 0
                               :foreground 0 :background 0 :rows ,(make-vector
                                       (1+ remote-display--max-fringe-rows)
                                       0)))
                :type 'args-out-of-range)
  (should-error (remote-display--encode
                 '(draw-fringe :frame 0 :x 0 :y 0 :width 0 :overlay 0 :cursor 0
                               :foreground 0 :background 0 :rows [#x10000]))
                :type 'args-out-of-range)
  ;; A frame that says it has one row too many.
  (let ((count (1+ remote-display--max-fringe-rows)))
    (should (equal (remotefns-tests--decode-error
                    (concat (unibyte-string 0 0 (ash (+ 27 (* 2 count)) -8)
                                            (logand (+ 27 (* 2 count)) #xff)
                                            6 0 0 0 0 0 0 0 0 0 0 0 0
                                            0 0 0 0 0 0 0 0 0 0 0 0
                                            (ash count -8) (logand count #xff))
                            (make-string (* 2 count) 0)))
                   '(bad-length)))))

(ert-deftest remotefns-tests-too-many-glyphs ()
  (skip-unless (featurep 'remote-display))
  (should-error (remote-display--encode
                 `(draw-glyphs :frame 0 :x 0 :y 0
                               ,@remotefns-tests--clip-and-color
                               :glyphs ,(make-vector
                                         (1+ remote-display--max-glyphs) 0)))
                :type 'args-out-of-range)
  ;; A frame with one glyph too many.
  (let ((count (1+ remote-display--max-glyphs)))
    (should (equal (remotefns-tests--decode-error
                    (concat (unibyte-string 0 0 (ash (+ 39 (* 4 count)) -8)
                                            (logand (+ 39 (* 4 count)) #xff)
                                            2 0 0 0 0 0 0 0 0 0 0 0 0
                                            0 0 0 0 0 0 0 0
                                            0 0 0 0 0 0 0 0 0 0 0 0
                                            0 0 0 0
                                            (ash count -8) (logand count #xff))
                            (make-string (* 4 count) 0)))
                   '(bad-length)))))

(ert-deftest remotefns-tests-glyph-out-of-range ()
  (skip-unless (featurep 'remote-display))
  (should-error (remote-display--encode
                 `(draw-glyphs :frame 0 :x 0 :y 0
                               ,@remotefns-tests--clip-and-color
                               :glyphs [#x100000000]))
                :type 'args-out-of-range)
  (should-error (remote-display--encode
                 `(draw-glyphs :frame 0 :x #x80000000 :y 0
                               ,@remotefns-tests--clip-and-color :glyphs []))
                :type 'args-out-of-range)
  (should-error (remote-display--encode
                 '(clear-area :frame 0 :x 0 :y 0 :width -1 :height 0))
                :type 'args-out-of-range)
  (should-error (remote-display--encode
                 '(copy-area :frame 0 :x 0 :y 0 :width 0 :height 0
                             :to-x 0 :to-y #x80000000))
                :type 'args-out-of-range)
  (should-error (remote-display--encode
                 '(draw-cursor :frame 0 :x 0 :y 0 :width 0 :height 0
                               :kind 256))
                :type 'args-out-of-range)
  (dolist (flag '(:overlay :cursor))
    (should-error (remote-display--encode
                   `(draw-fringe :frame 0 :x 0 :y 0 :width 0
                                 ,@(cl-substitute
                                    256 0 (list flag 0
                                                (if (eq flag :cursor)
                                                    :overlay :cursor)
                                                1)
                                    :count 1)
                                 :foreground 0 :background 0 :rows []))
                  :type 'args-out-of-range)))

(ert-deftest remotefns-tests-limits ()
  "The Lisp limits are the limits of the codec, and are constant."
  (skip-unless (featurep 'remote-display))
  (let ((glyphs `(draw-glyphs :frame 0 :x 0 :y 0
                              ,@remotefns-tests--clip-and-color
                              :glyphs ,(make-vector remote-display--max-glyphs
                                                    0)))
        (rows `(draw-fringe :frame 0 :x 0 :y 0 :width 0 :overlay 0 :cursor 0
                            :foreground 0 :background 0
                            :rows ,(make-vector remote-display--max-fringe-rows
                                                0))))
    (dolist (message (list glyphs rows))
      (should (equal (car (remote-display--decode
                           (remote-display--encode message)))
                     message))))
  (dolist (limit '(remote-display--max-glyphs remote-display--max-fringe-rows
                   remote-display--hash-size))
    (should-error (set limit 1) :type 'setting-constant)))

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

;; A value outside the range of a field is `args-out-of-range', also
;; if the value is a bignum.

(ert-deftest remotefns-tests-position-limits ()
  "A position field holds each signed 32-bit value."
  (skip-unless (featurep 'remote-display))
  (let ((low (- (ash 1 31))) (high (1- (ash 1 31))))
    (should (equal (remote-display--decode
                    (unibyte-string 0 0 0 25 3 0 0 0 1
                                    #x80 0 0 0 #x7f #xff #xff #xff
                                    0 0 0 0 0 0 0 0 0 0 0 0))
                   `((clear-area :frame 1 :x ,low :y ,high :width 0 :height 0
                                 :color 0)
                     . 29)))
    (dolist (value (list (1- low) (1+ high) (ash 1 70) (- (ash 1 70))))
      (should-error (remote-display--encode
                     `(clear-area :frame 0 :x ,value :y 0 :width 0 :height 0))
                    :type 'args-out-of-range))))

(ert-deftest remotefns-tests-size-limits ()
  "A size field holds each unsigned 32-bit value."
  (skip-unless (featurep 'remote-display))
  (let ((high (1- (ash 1 32))))
    (should (equal (car (remote-display--decode
                         (remote-display--encode
                          `(clear-area :frame 0 :x 0 :y 0
                                       :width ,high :height ,high :color 0))))
                   `(clear-area :frame 0 :x 0 :y 0
                                :width ,high :height ,high :color 0)))
    (dolist (value (list -1 (1+ high) (ash 1 70) (- (ash 1 70))))
      (should-error (remote-display--encode
                     `(clear-area :frame 0 :x 0 :y 0 :width ,value :height 0))
                    :type 'args-out-of-range))))

(defun remotefns-tests--with-field (message key value)
  "Return a copy of MESSAGE with VALUE as the field KEY."
  (cons (car message) (plist-put (copy-sequence (cdr message)) key value)))

(defconst remotefns-tests--position-keys
  '(:x :y :to-x :to-y :clip-x :clip-y)
  "The fields that hold a signed 32-bit value.
All other scalar fields are unsigned, with 32 bits or less.")

(defun remotefns-tests--bad-values (key)
  "Return values that are outside the range of the field KEY."
  (if (memq key remotefns-tests--position-keys)
      (list (ash 1 31) (- -1 (ash 1 31)) (ash 1 70))
    (list -1 (ash 1 32) (ash 1 70))))

(ert-deftest remotefns-tests-each-field-has-a-range ()
  "A value outside the range of a scalar field is an error, in each field."
  (skip-unless (featurep 'remote-display))
  (rd-gen-for-all
   #'remotefns-tests--message
   (lambda (message)
     (cl-loop for (key value) on (cdr message) by #'cddr
              when (integerp value)
              do (dolist (bad (remotefns-tests--bad-values key))
                   (should-error
                    (remote-display--encode
                     (remotefns-tests--with-field message key bad))
                    :type 'args-out-of-range))))))

(ert-deftest remotefns-tests-fringe-width-limits ()
  "The width field of a draw-fringe message has 16 bits."
  (skip-unless (featurep 'remote-display))
  (should-error (remote-display--encode
                 '(draw-fringe :frame 0 :x 0 :y 0 :width #x10000
                               :overlay 0 :cursor 0 :foreground 0 :background 0
                               :rows []))
                :type 'args-out-of-range))

(ert-deftest remotefns-tests-bignum-elements ()
  "A bignum in a vector of a message is out of range."
  (skip-unless (featurep 'remote-display))
  (should-error (remote-display--encode
                 `(draw-glyphs :frame 0 :x 0 :y 0
                               ,@remotefns-tests--clip-and-color
                               :glyphs [,(ash 1 70)]))
                :type 'args-out-of-range)
  (should-error (remote-display--encode
                 `(draw-fringe :frame 0 :x 0 :y 0 :width 0 :overlay 0
                               :cursor 0 :foreground 0 :background 0
                               :rows [,(ash 1 70)]))
                :type 'args-out-of-range))

(ert-deftest remotefns-tests-field-wrong-type ()
  "A field that is not an integer is `wrong-type-argument'."
  (skip-unless (featurep 'remote-display))
  (should-error (remote-display--encode
                 '(clear-area :frame 0 :x 1.5 :y 0 :width 0 :height 0))
                :type 'wrong-type-argument)
  (should-error (remote-display--encode
                 `(draw-glyphs :frame 0 :x 0 :y 0
                               ,@remotefns-tests--clip-and-color
                               :glyphs [a]))
                :type 'wrong-type-argument))

(ert-deftest remotefns-tests-array-wrong-type ()
  "An array field that is not a vector is `wrong-type-argument'."
  (skip-unless (featurep 'remote-display))
  (should-error (remote-display--encode
                 `(draw-glyphs :frame 0 :x 0 :y 0
                               ,@remotefns-tests--clip-and-color))
                :type 'wrong-type-argument)
  (should-error (remote-display--encode
                 `(draw-glyphs :frame 0 :x 0 :y 0
                               ,@remotefns-tests--clip-and-color
                               :glyphs 5))
                :type 'wrong-type-argument))

(ert-deftest remotefns-tests-unknown-message ()
  (skip-unless (featurep 'remote-display))
  (should-error (remote-display--encode '(no-such-message))
                :type 'remote-display-protocol-error)
  (should-error (remote-display--encode '(nil :version 1))
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
                 '(unknown-type 5))))

(ert-deftest remotefns-tests-type-byte-bounds ()
  "The valid type bytes start at 1 and stop at the last message type."
  (skip-unless (featurep 'remote-display))
  (let ((limit (1+ (length remote-display--message-formats))))
    (should (equal (remotefns-tests--decode-error (unibyte-string 0 0 0 1 0))
                   '(unknown-type 5)))
    (should (equal (remotefns-tests--decode-error
                    (unibyte-string 0 0 0 1 limit))
                   '(unknown-type 5)))
    ;; The last type is a type, and this frame is too short for it.
    (should (equal (remotefns-tests--decode-error
                    (unibyte-string 0 0 0 1 (1- limit)))
                   '(bad-length)))))

(defun remotefns-tests--unknown-frame (body)
  "Return a frame with the string BODY and a type byte that is not a type."
  (let ((length (1+ (length body))))
    (concat (unibyte-string 0 0 (ash length -8) (logand length #xff) 255)
            body)))

(ert-deftest remotefns-tests-skip-unknown-type ()
  "The error for an unknown type gives the size of the frame."
  (skip-unless (featurep 'remote-display))
  (rd-gen-for-all
   (lambda () (cons (rd-gen-bytes 20) (remotefns-tests--message)))
   (lambda (case)
     (let* ((frame (remotefns-tests--unknown-frame (car case)))
            (bytes (concat frame (remote-display--encode (cdr case))))
            (data (remotefns-tests--decode-error bytes)))
       (should (equal data (list 'unknown-type (length frame))))
       (should (equal (car (remote-display--decode bytes (length frame)))
                      (cdr case)))))))

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
                  (unibyte-string 0 0 0 17 3 0 0 0 1 0 0 0 0 0 0 0 0
                                  0 0 0 3))
                 '(bad-length)))
  ;; A copy-area frame without the to-y field.
  (should (equal (remotefns-tests--decode-error
                  (unibyte-string 0 0 0 25 4 0 0 0 1 0 0 0 0 0 0 0 0
                                  0 0 0 3 0 0 0 4 0 0 0 5))
                 '(bad-length)))
  ;; A draw-cursor frame without the kind.
  (should (equal (remotefns-tests--decode-error
                  (unibyte-string 0 0 0 21 5 0 0 0 1 0 0 0 0 0 0 0 0
                                  0 0 0 3 0 0 0 4))
                 '(bad-length)))
  ;; A draw-fringe frame with 2 rows and the bytes of only 1.
  (should (equal (remotefns-tests--decode-error
                  (unibyte-string 0 0 0 29 6 0 0 0 1 0 0 0 0 0 0 0 0
                                  0 8 0 0 0 0 0 0 0 0 0 0 0 2 0 1))
                 '(bad-length)))
  ;; A draw-glyphs frame with 2 glyphs and the bytes of only 1.
  (should (equal (remotefns-tests--decode-error
                  (unibyte-string 0 0 0 43 2 0 0 0 1 0 0 0 0 0 0 0 0
                                  0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
                                  0 0 0 0 0 0 0 0 0 2 0 0 0 65))
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
  ;; The size of the frame is known before the frame arrives.
  (should (equal (remotefns-tests--decode-error (unibyte-string 0 0 0 9 255))
                 '(unknown-type 13)))
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
                   (and (assq (car-safe (car result))
                              remotefns-tests--generators)
                        (<= 5 (cdr result) (length bytes)))))))))

(provide 'remotefns-tests)
;;; remotefns-tests.el ends here
