This application simulates the Chandy and Lamport snapshot algorithm that takes a snapshot of the whole system at any given time. It also includes a search utility that searches for a specific global state saved by the snapshot.
Several processes will be created within this system and they communicate by exchanging money and widgets. User can specify the number of processes as a command line argument.

To compile, simply:

$ make 


and run the program like:

$ ./processes <num_of_process> f

where <num_of_process> denotes the number of processes that will communicate in the system. and "f" means you want to preload all commands into the system.

Then, user should tell one of the process what it needs to send to which process. It is done by entering:

<from> <to> <money> <widgets>

For example, "1 2 300 30" will tell process #1 to send 300 dollars and 30 widgets to process #2. where <from> is the process that sends message, and <to> is the process that receives the message. <money> and <widgets> can be any integer you like.

Or user can tell process#0 to initiate a snapshot by entering:

"s" or "snapshot"

After entering several of these commands, user may wish to observe how the system executes these commands, then just enter:

"run"

And hit <enter> to observe the execution.



The program will stop at some point, at this point you should search for a specific snapshot that includes all the processes, enter:

"search <num>"
where the <num> is the num-th snapshot and then hit <enter>. You will see all the process and channel states associated with this snapshot. 

You can then simply add the money and widgets up in that snapshot to see if they equal to the original amount (money: 10000, widgets: 1000).


