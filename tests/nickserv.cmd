define srv irc.clan-dk.org:7701
define nickserv-nick NickServ-Ent
define nickserv %nickserv-nick%@srvx.clan-dk.org

# Log on, join testing channel
connect cl1 D00dm4n d00dm4n %srv% :Some Dude Man
:cl1 join #test

# Read a few help topics
:cl1 privmsg %nickserv-nick% :help
:cl1 expect %nickserv-nick% notice :?%nickserv-nick% Help?
:cl1 privmsg %nickserv-nick% :help account
:cl1 expect %nickserv-nick% notice :Account management commands are:
:cl1 privmsg %nickserv-nick% :help register
:cl1 expect %nickserv-nick% notice :See Also:

# Try to register (stumbling at first)
:cl1 privmsg %nickserv-nick% :register
:cl1 expect %nickserv-nick% notice :"/msg %nickserv% register"
:cl1 privmsg %nickserv% :register
:cl1 expect %nickserv-nick% notice :requires more parameters.
:cl1 privmsg %nickserv% :register D00dm4n sekrit
:cl1 expect %nickserv-nick% notice :Account.*registered
:cl1 privmsg %nickserv% :register D00dm4n-2 sekrit
:cl1 expect %nickserv-nick% notice :You're already authenticated.*rename your

# Connect another client and try to register there
connect cl2 D00dm4n-2 d00dm4n %srv% :Some Dude Man
:cl2 join #test
:cl2 privmsg %nickserv% :register D00dm4n sekrit
:cl2 expect %nickserv-nick% notice :Account.*already registered
:cl2 privmsg %nickserv% :register D00dm4n-2 sekrit
:cl2 expect %nickserv-nick% notice :Account.*been registered
:cl2 quit Cycling client

# .. now try to auth to an existing account
:cl1 privmsg %nickserv% :auth D00dm4n sekrit
:cl1 expect %nickserv-nick% notice :You are already authed.*reconnect
connect cl3 D00dm4n-2 d00dm4n %srv% :Some Dude Man
:cl3 privmsg %nickserv% :auth
:cl3 expect %nickserv-nick% notice :requires more parameters
:cl3 privmsg %nickserv% :auth D00dm4n-2 not-sekrit
:cl3 expect %nickserv-nick% notice :Incorrect password
:cl3 privmsg %nickserv% :auth D00dm4n-2 sekrit
:cl3 expect %nickserv-nick% notice :I recognize you.

# change some handle settings
:cl1 privmsg %nickserv% :pass not-sekrit s00p3r-sekrit
:cl1 expect %nickserv-nick% :Incorrect password
:cl1 privmsg %nickserv% :pass sekrit s00p3r-sekrit
:cl1 expect %nickserv-nick% :Password changed
:cl1 privmsg %nickserv-nick% :set
:cl1 expect %nickserv-nick% :account settings
:cl1 privmsg %nickserv-nick% :set bad-option
:cl1 expect %nickserv-nick% :invalid account setting
:cl1 privmsg %nickserv-nick% :set info
:cl1 expect %nickserv-nick% :?info:
:cl1 privmsg %nickserv-nick% :set info Test infoline with unique pattern
:cl1 expect %nickserv-nick% :info:.*Test infoline with unique pattern

# check account info
:cl1 privmsg %nickserv-nick% :handleinfo
:cl1 expect %nickserv-nick% :Current nickname
:cl1 privmsg %nickserv-nick% :handleinfo *d00dm4n
:cl1 expect %nickserv-nick% :Current nickname
:cl1 privmsg %nickserv-nick% :handleinfo *d00dm4n-2
:cl1 expect %nickserv-nick% :Infoline
:cl1 privmsg %nickserv-nick% :userinfo d00dm4n-2
:cl1 expect %nickserv-nick% :is authenticated to account Entrope.

# miscellaneous other commands
:cl1 privmsg %nickserv-nick% :vacation
:cl1 expect %nickserv-nick% :You are now on vacation
:cl1 privmsg %nickserv-nick% :status
:cl1 expect %nickserv-nick% :registered globally

# Unregister our account(s) so we can repeat the script later
sync cl1,cl3
:cl1 privmsg %nickserv% :unregister s00p3r-sekrit
:cl3 privmsg %nickserv% :unregister sekrit
