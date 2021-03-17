# Web_Server
1)	Seth Jacobs 800293004 Elie Benzaquen 800504053 Binyamin Jachter 800497083


2)	Word Divided: We all worked together on zoom that way we all could put in our input and understand how each aspect of the assignment will work. 

3)	We created a pool of threads that stores the number of threads determined by the command line argument. 
We created a queue to store all the calls. If FIFO the queue will work in a normal fashion putting calls into the queue at the top and each threads taking them out in order. If it is HPIC we add the images to the front of the queue and the non-images to back. Doing this always the images to have a higher priority than the non-images. If it is HPHC we add the non-image files to the front of the queue and the images to the back this way the non-images have preference.    
We have a thread struct that an id for the thread, and a count for what type of request it is. 

When the server starts it creates all the threads determined by the command line argument and adds them to the thread pool. The server then receives requests from the client and adds them to the queue. Depending on the scheduling the sever then assignees a thread to each thing in the queue and the thread runs it. 


4)	We made the Any call for scheduling the same as FIFO. 
FIFO vs CONCUR.  When it is FIFO, we make the semaphore equal one and therefore only one thread can run at a time. If it is CONCUR, then the semaphore is equal to the number of threads so they can all run simulteniasly. 


5) Testing:
We ran our server with a get call and looked at the times and each thread overlapped start and finish times. 
We ran mutliple processes of client on our server to make sure our server can handle multiple calls at once, and it was able to run 15 calls at a time without a cap in sight. 
6) Bugs:
Client was working however in the receive method it started to get stuck and we were unsure the reason why. 
