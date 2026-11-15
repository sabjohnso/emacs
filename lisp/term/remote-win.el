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

;;;; Listeners

(declare-function remote-display-attach "remoteproc.c" (display process))
(declare-function remote-display-client "remoteproc.c" (display))

(defvar remote-display--listeners nil
  "The listeners of the sessions.
Each element is (DISPLAY SERVER DIRECTORY): the name of a session, the
server process of its socket, and the directory of the socket.")

(defun remote-display-listen (display)
  "Make a socket for the clients of the session DISPLAY, and return its name.
The socket is in a new directory that only the user can use.  Each
connection to the socket becomes the client of DISPLAY, and detaches
the client before it.  When the session closes, the socket and its
directory are deleted, and the connection of the client closes.  A
session can have only one socket."
  (when (assoc display remote-display--listeners)
    (error "Session %s already has a socket" display))
  (let* ((directory (remote-display--make-socket-directory))
         (socket (remote-display--socket-in directory))
         (server (condition-case err
                     (remote-display--start-server display socket)
                   (error (delete-directory directory t)
                          (signal (car err) (cdr err))))))
    (push (list display server directory) remote-display--listeners)
    socket))

(defun remote-display--stop-listening (terminal)
  "Delete the server of TERMINAL, its socket, and the directory of the socket.
Do nothing for a terminal with no socket."
  (let ((listener (assoc (terminal-name terminal) remote-display--listeners)))
    (when listener
      (setq remote-display--listeners
            (delq listener remote-display--listeners))
      (remote-display--delete-listener listener))))

(defun remote-display--delete-listener (listener)
  "Delete the server, the socket, and the directory of LISTENER.
LISTENER is an element of `remote-display--listeners'."
  (pcase-let ((`(,_display ,server ,directory) listener))
    (unwind-protect
        (delete-process server)
      (delete-directory directory t))))

(defun remote-display--make-socket-directory ()
  "Make a new directory for a socket, and return its name.
`make-temp-file' gives a new directory the mode 700, so only the user
can use it."
  (make-temp-file (expand-file-name "remote-display-"
                                    (or (getenv "XDG_RUNTIME_DIR")
                                        temporary-file-directory))
                  t))

(defun remote-display--start-server (display socket)
  "Start a server at the file SOCKET, and return its process.
Each connection to the server becomes the client of DISPLAY.  Emacs
does not ask about the server when it exits."
  (make-network-process
   :name (format "remote-display-listen %s" display)
   :server t :family 'local :service socket :noquery t
   :log (lambda (_server connection _message)
          (remote-display-attach display connection))))

(defun remote-display--close-client (terminal)
  "Close the connection of the client of TERMINAL, if TERMINAL is a session.
Thus the client knows that the session ended, and can stop."
  (when (eq (terminal-live-p terminal) 'remote)
    (let ((client (remote-display-client (terminal-name terminal))))
      (when client
        (delete-process client)))))

(defun remote-display--end-session (terminal)
  "Stop the listener of TERMINAL, and then close the connection of its client.
The relay of \"remote-client --ssh\" exits when the session closes the
connection and the socket does not exist, so the socket goes first.
This function is in `delete-terminal-functions', so it runs for each
terminal."
  (remote-display--stop-listening terminal)
  (remote-display--close-client terminal))

(add-hook 'delete-terminal-functions #'remote-display--end-session)

(defun remote-display--stop-all-listeners ()
  "Delete the server, the socket, and the directory of each session.
`kill-emacs' does not delete the terminals, so this function is in
`kill-emacs-hook'.  An error for one session does not stop the
others."
  (dolist (listener remote-display--listeners)
    (with-demoted-errors "remote-display: %S"
      (remote-display--delete-listener listener)))
  (setq remote-display--listeners nil))

(add-hook 'kill-emacs-hook #'remote-display--stop-all-listeners)

;;;; Start

(defun remote-display-start ()
  "Start a new session with one frame, and return the name of its socket.
A client of the socket shows the frame.  The command
\"remote-client --ssh HOST\" calls this function through emacsclient on
HOST.

The new frame becomes the selected frame.  In a daemon, the selected
frame is otherwise the initial frame, and redisplay does nothing while
the initial frame is selected."
  (let ((display (remote-display--new-name)))
    (x-open-connection display)
    (select-frame (make-frame-on-display display))
    (remote-display-listen display)))

(defun remote-display-resume (display)
  "Return the name of the socket of the open session DISPLAY.
A new client of the socket shows the frames of the session again.  The
command \"remote-client --ssh HOST --session=DISPLAY\" calls this
function through emacsclient on HOST.  Signal an error if DISPLAY is
not an open session with a socket."
  (let ((listener (assoc display remote-display--listeners)))
    (unless listener
      (error "No open session %s" display))
    (remote-display--listener-socket listener)))

(defun remote-display--listener-socket (listener)
  "Return the name of the socket of LISTENER.
LISTENER is an element of `remote-display--listeners'."
  (pcase-let ((`(,_display ,_server ,directory) listener))
    (remote-display--socket-in directory)))

(defun remote-display--socket-in (directory)
  "Return the name of the socket of a session in DIRECTORY."
  (expand-file-name "socket" directory))

(defun remote-display--new-name ()
  "Return the name of a session that is not open."
  (let ((number 0) (name nil))
    (while (member (setq name (format "remote-%d" (cl-incf number)))
                   (x-display-list)))
    name))

(provide 'remote-win)
(provide 'term/remote-win)

;;; remote-win.el ends here
