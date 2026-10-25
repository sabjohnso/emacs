;;; remote-win.el --- remote display window system support  -*- lexical-binding: t -*-

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

;; The remote display window system shows frames on a client program
;; through the remote display protocol.  The name of a remote display
;; session starts with "remote".

;;; Code:

(eval-when-compile (require 'cl-lib))
(require 'term/common-win)
(require 'frame)
(require 'faces)
(require 'fontset)

(declare-function x-open-connection "remoteterm.c"
                  (display &optional xrm-string must-succeed))

(cl-defmethod window-system-initialization (&context (window-system remote)
                                            &optional display)
  "Initialize the remote display window system.
DISPLAY is the name of the first session."
  (create-default-fontset)
  (condition-case err
      (create-fontset-from-fontset-spec standard-fontset-spec t)
    (error (display-warning
            'initialization
            (format "Creation of the standard fontset failed: %s" err)
            :error)))
  (x-open-connection (or display x-display-name)))

(add-to-list 'display-format-alist '("\\`remote" . remote))

(cl-defmethod handle-args-function (args &context (window-system remote))
  (x-handle-args args))

(cl-defmethod frame-creation-function (params &context (window-system remote))
  (x-create-frame-with-faces params))

(provide 'remote-win)
(provide 'term/remote-win)

;;; remote-win.el ends here
