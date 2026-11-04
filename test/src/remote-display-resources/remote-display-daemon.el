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
;;
;; A property test has many trials, and a call of emacsclient for each
;; trial is slow.  So `rd-daemon-deftest' sends one form, and the
;; daemon runs all trials.  The daemon loads the test file for this,
;; and the test file loads the generators.  The batch Emacs gets the
;; result when the form returns.  If the daemon crashes, a file next
;; to the socket tells which trial was running.

;;; Code:

(require 'ert)

(declare-function rd-gen-check-report "remote-display-generators" (report))
(declare-function rd-gen-last-trial "remote-display-generators" (trial-file))

(defconst rd-daemon-timeout 30
  "Number of seconds to wait for the daemon to start.")

(defconst rd-daemon-gc-threshold (ash 1 26)
  "The value of `gc-cons-threshold' in the daemon.
With the default value, garbage collection takes about half of the
time of a drawing test.  This value stops the collections of a test,
so each daemon collects garbage at fixed places: see
`rd-daemon--setup-form' and `rd-daemon--check-collection'.")

(defvar rd-daemon--socket nil
  "The socket file of the daemon of `rd-with-daemon'.")

(defvar rd-daemon--no-report nil
  "Non-nil if the daemon of `rd-with-daemon' did not give a report.")

(defmacro rd-with-daemon (&rest body)
  "Start an Emacs daemon, evaluate BODY, and then stop the daemon.
In BODY, call `rd-daemon-eval' to evaluate a form in the daemon.  The
daemon collects garbage when it deletes its first frame, and after
BODY.  The current ERT test fails if the daemon does not do the
collection after BODY, or crashes in it."
  (declare (indent 0))
  `(rd-daemon--call (lambda () ,@body)))

(defmacro rd-daemon-deftest (name docstring &rest body)
  "Define the ERT test NAME, with DOCSTRING, whose BODY runs in a daemon.
The test is skipped in a build without the test functions of the
remote display backend: see --enable-remote-display-tests.  BODY
is the body of a new function, so the file of the test compiles it.
The test fails if BODY fails, if no trial of `rd-gen-for-all' finishes,
or if the daemon crashes or does not collect garbage.  A failure names
the trial that failed."
  (declare (indent 1) (doc-string 2))
  (let ((function (intern (format "%s--in-daemon" name))))
    `(progn
       (defun ,function () ,@body)
       (ert-deftest ,name ()
         ,docstring
         (skip-unless (featurep 'remote-test))
         (rd-with-daemon (rd-daemon-check #',function))))))

(defun rd-daemon-check (function)
  "Call FUNCTION in the daemon, and give the result to the current ERT test.
FUNCTION must be a symbol, as for `rd-daemon-report'."
  (rd-gen-check-report (rd-daemon-report function)))

(defun rd-daemon-report (function)
  "Call FUNCTION in the daemon, and return the value of `rd-gen-report'.
FUNCTION must be a symbol with a definition in a loaded file.  This
call of emacsclient is the only one, so FUNCTION can run all trials of
a test.  If the daemon gives no value, for example after a crash, the
value is a report with :failure, and with the :trial that was running."
  (let ((trial-file (concat rd-daemon--socket ".trial")))
    (delete-file trial-file)
    (condition-case failure
        (rd-daemon-eval (rd-daemon--report-form function trial-file))
      (error (setq rd-daemon--no-report t)
             (list :trial (rd-gen-last-trial trial-file)
                   :failure (format "The daemon gave no report: %S"
                                    failure))))))

(defun rd-daemon--report-form (function trial-file)
  "Return the form that loads the file of FUNCTION and calls `rd-gen-report'.
The daemon loads the compiled form of the file if that is not older
than the source, because the compiled form is much faster to load."
  `(let ((load-prefer-newer t))
     (load ,(file-name-sans-extension (symbol-file function 'defun)) nil t)
     (rd-gen-report #',function ,trial-file)))

(defun rd-daemon--call (function)
  "Start an Emacs daemon, call FUNCTION, and then stop the daemon.
If the daemon gave no report, the report has the failure already, and
the daemon can be dead, so do not ask it to collect garbage."
  (let* ((directory (make-temp-file "rd-daemon" t))
         (rd-daemon--socket (expand-file-name "server" directory))
         (rd-daemon--no-report nil)
         (process (rd-daemon--start rd-daemon--socket)))
    (unwind-protect
        (progn
          (funcall function)
          (unless rd-daemon--no-report
            (rd-daemon--check-collection)))
      (rd-daemon--stop process)
      (delete-directory directory t))))

(defun rd-daemon--check-collection ()
  "Fail the current ERT test if the daemon does not collect garbage."
  (should (> (rd-daemon-eval '(let ((collections gcs-done))
                                (garbage-collect)
                                (- gcs-done collections)))
             0)))

(defun rd-daemon--stop (process)
  "Stop the daemon PROCESS, wait until it exits, and kill its buffer."
  (delete-process process)
  (while (process-live-p process)
    (accept-process-output nil 0.01))
  (kill-buffer (process-buffer process)))

(defun rd-daemon-eval (form)
  "Evaluate FORM in the daemon, and return the value.
Code that already runs in the daemon evaluates FORM directly.  In
other code, the value must have a printed form that `read' can read."
  (if (daemonp)
      (eval form t)
    (rd-daemon--eval-with-client form)))

(defun rd-daemon--eval-with-client (form)
  "Evaluate FORM in the daemon through emacsclient, and return the value."
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
                                "-Q" (concat "--fg-daemon=" socket)
                                "--eval"
                                (prin1-to-string (rd-daemon--setup-form)))))
    (with-timeout (rd-daemon-timeout
                   (delete-process process)
                   (error "The daemon did not start"))
      (while (not (file-exists-p socket))
        (accept-process-output process 0.1)))
    process))

(defun rd-daemon--setup-form ()
  "Return the form that a new daemon evaluates.
The daemon has few collections, so make it collect when it deletes its
first frame, because that frame is live and has drawn.  The daemon has
not loaded this file, so the form must not call a function of it."
  `(progn
     (setq gc-cons-threshold ,rd-daemon-gc-threshold)
     (letrec ((collect (lambda (_frame)
                         (remove-hook 'delete-frame-functions collect)
                         (garbage-collect))))
       (add-hook 'delete-frame-functions collect))))

(defun rd-daemon--program (name)
  "Return the file name of program NAME in the build tree."
  (expand-file-name name
                    (file-name-directory
                     (directory-file-name invocation-directory))))

(provide 'remote-display-daemon)
;;; remote-display-daemon.el ends here
