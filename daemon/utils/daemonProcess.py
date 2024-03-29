#!/usr/bin/python
import os               
import sys          
import time

""" Default daemon parameters."""
UMASK = 0        # File mode creation mask of the daemon.
WORKDIR = "/"    # Default working directory for the daemon.
MAXFD = 1024     # Default maximum for the number of available file descriptors.
""" The standard I/O file descriptors are redirected to /dev/null by default."""

if (hasattr(os, "devnull")):
    REDIRECT_TO = os.devnull
else:
    REDIRECT_TO = "/dev/null"

def create():
   """ Detach a process from the controlling terminal and run it in the
   background as a daemon.   
   INPUT:
   OUTPUT: A Background system process on UNIX which runs independently 
   of any user who is logged-in."""
   print "Starting the Daemon Server ..."
   startingAt = time.time()
   try:
       """ Fork a child process so the parent can exit.  This returns control to
       the command-line or shell.  It also guarantees that the child will not
       be a process group leader, since the child receives a new process ID
       and inherits the parent's process group ID.  This step is required
       to insure that the next call to os.setsid is successful."""
       pid = os.fork()
   except OSError, e:
      raise Exception, "%s [%d]" % (e.strerror, e.errno)
      print "Exception occured while forking " + "%s [%d]" % (e.strerror, e.errno)
      sys.exit(False)

   if (pid == 0):    # The first child.
       """ To become the session leader of this new session and the process 
       group leader of the new process group, we call os.setsid().  The 
       process is also guaranteed not to have a controlling terminal."""
       os.setsid()

       """ Is ignoring SIGHUP necessary?
       It's often suggested that the SIGHUP signal should be ignored before
       the second fork to avoid premature termination of the process.  The
       reason is that when the first child terminates, all processes, e.g.
       the second child, in the orphaned group will be sent a SIGHUP.
       
       "However, as part of the session management system, there are exactly
       two cases where SIGHUP is sent on the death of a process:
       
       1) When the process that dies is the session leader of a session that
          is attached to a terminal device, SIGHUP is sent to all processes
          in the foreground process group of that terminal device.
       2) When the death of a process causes a process group to become
          orphaned, and one or more processes in the orphaned group are
          stopped, then SIGHUP and SIGCONT are sent to all members of the
          orphaned group." [2]
       
       The first case can be ignored since the child is guaranteed not to have
       a controlling terminal.  The second case isn't so easy to dismiss.
       The process group is orphaned when the first child terminates and
       POSIX. 1 requires that every STOPPED process in an orphaned process
       group be sent a SIGHUP signal followed by a SIGCONT signal.  Since the
       second child is not STOPPED though, we can safely forego ignoring the
       SIGHUP signal.  In any case, there are no ill-effects if it is ignored.
       
       import signal           # Set handlers for asynchronous events.
       signal.signal(signal.SIGHUP, signal.SIG_IGN)"""
       try:
           """ Fork a second child and exit immediately to prevent zombies.  
           This causes the second child process to be orphaned, making the 
           init process responsible for its cleanup.  And, since the first 
           child is a session leader without a controlling terminal, it's 
           possible for it to acquire one by opening a terminal in the future
           (System V-based systems).  This second fork guarantees that the 
           child is no longer a session leader, preventing the daemon from 
           ever acquiring a controlling terminal."""
           pid = os.fork()    # Fork a second child.
       except OSError, e:
           raise Exception, "%s [%d]" % (e.strerror, e.errno)
           print "Exception occured while forking inside the first child " \
                  + "%s [%d]" % (e.strerror, e.errno)
           sys.exit(False)

       if (pid == 0):    # The second child.
           """ Since the current working directory may be a mounted filesystem
           , we avoid the issue of not being able to unmount the filesystem at
           shutdown time by changing it to the root directory."""
           os.chdir(WORKDIR)
           """ We probably don't want the file mode creation mask inherited 
           from the parent, so we give the child complete control over 
           permissions."""
           os.umask(UMASK)
       else:
           # exit() or _exit()?  See below.
           #print "Exit parent (the first child) of the second child"
           os._exit(True)    
          
   else:
      
      #print "Exit parent of the first child."
      os._exit(True)    

   """Close all open file descriptors.  This prevents the child from keeping
   open any file descriptors inherited from the parent.  There is a variety
   of methods to accomplish this task.  Three are listed below.
   
   Use the getrlimit method to retrieve the maximum file descriptor number
   that can be opened by this process.  If there is not limit on the
   resource, use the default value."""
   
   import resource        # Resource usage information.
   maxfd = resource.getrlimit(resource.RLIMIT_NOFILE)[1]
   if (maxfd == resource.RLIM_INFINITY):
      maxfd = MAXFD
  
   startedAt = time.time()
   #print "Daemon started ("+ str("%3f"% startedAt - startingAt) + ")"  
   print 'Daemon started in %1.4f Sec(s)'% (startedAt - startingAt)
   # print "Iterate through and closing all file descriptors."
   for fd in range(0, maxfd):
      try:
         os.close(fd)
      except OSError:    
         print "ERROR, fd wasn't open to begin with (ignored)"

   """Redirect the standard I/O file descriptors to the specified file.  
   Since the daemon has no controlling terminal, most daemons redirect 
   stdin, stdout, and stderr to /dev/null.  This is done to prevent 
   side-effects from reads and writes to the standard I/O file 
   descriptors. This call to open is guaranteed to return the lowest 
   file descriptor, which will be 0 (stdin), since it was closed above."""
   # print "Duplicating standard input to standard output and standard error"
   os.open(REDIRECT_TO, os.O_RDWR)
   os.dup2(0, 1)            # standard output (1)
   os.dup2(0, 2)            # standard error (2)

   return(True)