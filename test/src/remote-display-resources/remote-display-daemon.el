;;; remote-display-daemon.el --- run test code in an Emacs daemon  -*- lexical-binding:t -*-

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

;; Redisplay does not run in batch mode, so a test that must see a
;; frame draw cannot run in the batch Emacs of the test suite.  This
;; file starts the Emacs under test as a daemon, which is not in batch
;; mode, and evaluates forms in it through emacsclient.

;;; Code:

(require 'ert)

(defconst rd-daemon-timeout 30
  "Number of seconds to wait for the daemon to start.")

(defvar rd-daemon--socket nil
  "The socket file of the daemon of `rd-with-daemon'.")

(defmacro rd-with-daemon (&rest body)
  "Start an Emacs daemon, evaluate BODY, and then stop the daemon.
In BODY, call `rd-daemon-eval' to evaluate a form in the daemon."
  (declare (indent 0))
  `(rd-daemon--call (lambda () ,@body)))

(defun rd-daemon--call (function)
  "Start an Emacs daemon, call FUNCTION, and then stop the daemon."
  (let* ((directory (make-temp-file "rd-daemon" t))
         (rd-daemon--socket (expand-file-name "server" directory))
         (process (rd-daemon--start rd-daemon--socket)))
    (unwind-protect
        (funcall function)
      (rd-daemon--stop process)
      (delete-directory directory t))))

(defun rd-daemon--stop (process)
  "Stop the daemon PROCESS, wait until it exits, and kill its buffer."
  (delete-process process)
  (while (process-live-p process)
    (accept-process-output nil 0.01))
  (kill-buffer (process-buffer process)))

(defun rd-daemon-eval (form)
  "Evaluate FORM in the daemon, and return the value.
The value must have a printed form that `read' can read."
  (with-temp-buffer
    (let ((status (call-process (rd-daemon--program "lib-src/emacsclient")
                                nil t nil
                                "-s" rd-daemon--socket
                                "--eval" (prin1-to-string
                                          (rd-daemon--printed form)))))
      (unless (eql status 0)
        (error "emacsclient failed: %s" (buffer-string)))
      (car (read-from-string
            (decode-coding-string
             (base64-decode-string (car (read-from-string (buffer-string))))
             'utf-8-emacs-unix))))))

(defun rd-daemon--printed (form)
  "Return a form that gives the printed value of FORM in base64.
emacsclient stops its output at a null byte, and base64 has none."
  `(base64-encode-string
    (encode-coding-string (prin1-to-string ,form) 'utf-8-emacs-unix)
    t))

(defun rd-daemon--start (socket)
  "Start a daemon with the server SOCKET, and return its process.
Wait until the daemon accepts connections."
  (let ((process (start-process "rd-daemon" " *rd-daemon*"
                                (rd-daemon--program "src/emacs")
                                "-Q" (concat "--fg-daemon=" socket))))
    (with-timeout (rd-daemon-timeout
                   (delete-process process)
                   (error "The daemon did not start"))
      (while (not (file-exists-p socket))
        (accept-process-output process 0.1)))
    process))

(defun rd-daemon--program (name)
  "Return the file name of program NAME in the build tree."
  (expand-file-name name
                    (file-name-directory
                     (directory-file-name invocation-directory))))

(provide 'remote-display-daemon)
;;; remote-display-daemon.el ends here
