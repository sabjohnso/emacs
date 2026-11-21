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
(require 'term/remote-tip)

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
(declare-function remote-display--queue-end "remoteproc.c" (display))
(declare-function remote-display--drain "remoteproc.c" (clients limit))

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

(defun remote-display--end-session (terminal)
  "End the session TERMINAL: stop its listener, and then end its client.
Do nothing if TERMINAL is not a session.  The relay of
\"emacs-remote-client --ssh\" exits when the session closes the
connection and the socket does not exist, so the socket goes first.
This function is in `delete-terminal-functions', so it runs for each
terminal."
  (when (eq (terminal-live-p terminal) 'remote)
    (remote-display--stop-listening terminal)
    (remote-display--end-clients (list terminal))))

(add-hook 'delete-terminal-functions #'remote-display--end-session)

(defun remote-display--end-all-sessions ()
  "End each session as `remote-display--end-session' does.
`kill-emacs' does not delete the terminals, so this function is in
`kill-emacs-hook'.  Thus the client of each session gets the end of
the session, and each socket and its directory go away.  The clients
share one deadline, so a quit waits at most
`remote-display--end-limit' seconds for all of them.  An error for one
session does not stop the others."
  (let ((sessions (seq-filter (lambda (terminal)
                                (eq (terminal-live-p terminal) 'remote))
                              (terminal-list))))
    (dolist (terminal sessions)
      (with-demoted-errors "remote-display: %S"
        (remote-display--stop-listening terminal)))
    (remote-display--end-clients sessions))
  (remote-display--stop-all-listeners))

(defconst remote-display--end-limit 2
  "The longest wait in seconds for the clients to get the end of a session.")

(defun remote-display--end-clients (terminals)
  "Send the end of the session to the client of each of TERMINALS.
First put the end-session message in the queue of each client.  Then
wait until each client got all its bytes, but at most
`remote-display--end-limit' seconds for all of them.  Then close each
connection.  Thus each client gets the end before the close, and
knows that the session ended.  An error for one client does not stop
the others."
  (let ((clients (delq nil (mapcar (lambda (terminal)
                                     (with-demoted-errors "remote-display: %S"
                                       (remote-display--queue-end
                                        (terminal-name terminal))))
                                   terminals))))
    (remote-display--drain clients remote-display--end-limit)
    (dolist (client clients)
      (with-demoted-errors "remote-display: %S"
        (delete-process client)))))

(defun remote-display--stop-all-listeners ()
  "Delete the server, the socket, and the directory of each session.
An error for one session does not stop the others."
  (dolist (listener remote-display--listeners)
    (with-demoted-errors "remote-display: %S"
      (remote-display--delete-listener listener)))
  (setq remote-display--listeners nil))

(add-hook 'kill-emacs-hook #'remote-display--end-all-sessions)

;;;; Kill terminal

(defun remote-display-save-buffers-kill-terminal (arg)
  "Offer to save each buffer, then end the session of the selected frame.
This is what `save-buffers-kill-terminal' does on a remote frame, as
on a frame of emacsclient: Emacs does not exit, and the other sessions
stay.  The client of the session gets the end of the session, and
stops.  If the session is the only terminal of Emacs, Emacs cannot
delete it, so call `kill-emacs': `kill-emacs-hook' ends the session.
Each session has this function in its terminal parameter
`save-buffers-kill-terminal-function'.  With ARG non-nil, save all
file-visiting buffers with no question."
  (save-some-buffers arg)
  (let ((terminal (frame-terminal)))
    (if (remote-display--only-terminal-p terminal)
        (kill-emacs)
      (delete-terminal terminal))))

(defun remote-display--only-terminal-p (terminal)
  "Return non-nil if TERMINAL is the only live terminal.
A daemon keeps its initial terminal, so it always has another one."
  (equal (terminal-list) (list terminal)))

;;;; Selections

;; The client sends each change of its clipboard, and remoteclip.c
;; keeps the text in the terminal parameter `remote-clipboard'.  A kill
;; puts the text there too, and sends it to the client.  Only CLIPBOARD
;; goes to the client.  PRIMARY and SECONDARY stay inside Emacs, in
;; the terminal parameter `remote-selections'.

(declare-function remote-display--send-clipboard "remoteclip.c"
                  (display text))

(defconst remote-display--text-targets
  '(UTF8_STRING STRING TEXT COMPOUND_TEXT text/plain\;charset=utf-8)
  "The targets of a selection that give its text.")

(cl-defmethod gui-backend-set-selection (selection value
                                         &context (window-system remote))
  (let ((terminal (frame-terminal)))
    (if (eq selection 'CLIPBOARD)
        (progn
          (set-terminal-parameter terminal 'remote-clipboard value)
          (set-terminal-parameter terminal 'remote-clipboard-owned
                                  (and value t))
          (when (stringp value)
            ;; A kill must not fail because of the clipboard of the
            ;; client, so a text above the limit stays inside Emacs.
            (condition-case err
                (remote-display--send-clipboard (terminal-name terminal)
                                                value)
              (remote-display-clipboard-too-large
               (message "%s" (error-message-string err))))))
      (set-terminal-parameter
       terminal 'remote-selections
       (cons (cons selection value)
             (assq-delete-all selection
                              (copy-sequence (terminal-parameter
                                              terminal
                                              'remote-selections))))))))

(cl-defmethod gui-backend-get-selection (selection target-type
                                         &context (window-system remote))
  (let ((value (remote-display--selection selection)))
    (and (stringp value)
         (memq target-type remote-display--text-targets)
         value)))

(cl-defmethod gui-backend-selection-owner-p (selection
                                             &context (window-system remote))
  (if (eq selection 'CLIPBOARD)
      (terminal-parameter (frame-terminal) 'remote-clipboard-owned)
    (and (remote-display--selection selection) t)))

(cl-defmethod gui-backend-selection-exists-p (selection
                                              &context (window-system remote))
  (and (remote-display--selection selection) t))

(defun remote-display--selection (selection)
  "Return the value of SELECTION in the session of the selected frame."
  (let ((terminal (frame-terminal)))
    (if (eq selection 'CLIPBOARD)
        (terminal-parameter terminal 'remote-clipboard)
      (alist-get selection (terminal-parameter terminal
                                               'remote-selections)))))

;;;; Scale

;; A client sends its scale, and remoteterm.c gives the session the
;; resolution of that scale.  After a change, the session gives a
;; `config-changed-event' with the name of the session, as xsettings.c
;; does for a change of Xft/DPI.  The arg is `remote-first-scale' for
;; the first scale of a connection, before the client shows the frames,
;; and `font-render' for a later change.  The event of another display
;; goes to the handler that was bound before this file, for example
;; that of dynamic-setting.el.

(declare-function reconsider-frame-fonts "frame.c" (frame))

(defvar remote-display--next-config-changed-handler
  (lookup-key special-event-map [config-changed-event])
  "The handler of `config-changed-event' before remote-win.el.
`remote-display-handle-config-changed-event' gives it each event whose
display is not a remote session.")

(defun remote-display-handle-config-changed-event (event)
  "Handle the `config-changed-event' EVENT.
If the display of EVENT is a remote display session, open the fonts of
each frame of the session again at its new resolution, and draw each
frame again.  After the first scale of a connection (the arg
`remote-first-scale'), each frame keeps its columns and lines, and the
session sends its new pixel size to the client.  After a later change
\(the arg `font-render'), each frame keeps its size in pixels, because
the client sends the new size of its window.  Give each other event to
`remote-display--next-config-changed-handler'."
  (interactive "e")
  (let ((display (nth 2 event)))
    (if (remote-display--session-p display)
        (pcase (nth 1 event)
          ('remote-first-scale (remote-display--reopen-fonts display nil))
          ('font-render (remote-display--reopen-fonts display 'force)))
      (when (functionp remote-display--next-config-changed-handler)
        (funcall remote-display--next-config-changed-handler event)))))

(define-key special-event-map [config-changed-event]
  #'remote-display-handle-config-changed-event)

(defun remote-display--session-p (display)
  "Return non-nil if DISPLAY is the name of an open remote session."
  (seq-some (lambda (terminal)
              (and (eq (terminal-live-p terminal) 'remote)
                   (equal (terminal-name terminal) display)))
            (terminal-list)))

(defun remote-display--reopen-fonts (display inhibit-resize)
  "Open the fonts of each frame of the session DISPLAY again, and redraw it.
Bind `frame-inhibit-implied-resize' to INHIBIT-RESIZE: nil keeps the
columns and the lines of each frame, and `force' keeps its size in
pixels, also for a frame that does not have its first size yet."
  (clear-font-cache)
  (let ((frame-inhibit-implied-resize inhibit-resize))
    (dolist (frame (remote-display--session-frames display))
      (reconsider-frame-fonts frame)
      (redraw-frame frame))))

(defun remote-display--session-frames (display)
  "Return the live frames of the session DISPLAY."
  (filtered-frame-list
   (lambda (frame)
     (and (eq (framep frame) 'remote)
          (equal (terminal-name (frame-terminal frame)) display)))))

;;;; Start

(defun remote-display-start ()
  "Start a new session with one frame, and return the name of its socket.
A client of the socket shows the frame.  The command
\"emacs-remote-client --ssh HOST\" calls this function through
emacsclient on HOST.

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
command \"emacs-remote-client --ssh HOST --session=DISPLAY\" calls
this function through emacsclient on HOST.  Signal an error if DISPLAY
is not an open session with a socket."
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
