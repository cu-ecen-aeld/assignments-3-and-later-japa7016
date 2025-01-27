#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>

int main(int argc, char *argv[])
{
	openlog("writer", LOG_PID | LOG_CONS, LOG_USER);
	
	if(argc != 3)
	{
	        syslog(LOG_ERR, "Error: Two arguments required: <filename> <string>");
        	fprintf(stderr, "Usage: %s <filename> <string>\n", argv[0]);
        	closelog();
        	return 1;	
	}
	
	
    	const char *writefile = argv[1];
    	const char *writestr = argv[2];
    	
    	 syslog(LOG_DEBUG, "Writing \"%s\" to %s",writestr, writefile);
    	 
    	FILE *file = fopen(writefile, "w");
	if (!file) 
	{
        	syslog(LOG_ERR, "Error opening file %s: %s", writefile, strerror(errno));
        	perror("Error");
        	closelog();
        	return 1;
        }
        if (fprintf(file, "%s", writestr) < 0) 
        {
        	syslog(LOG_ERR, "Error writing to file %s: %s", writefile, strerror(errno));
        	perror("Error");
        	fclose(file);
        	closelog();
        	return 1;
    	}
        // Close the file
    	fclose(file);
    	syslog(LOG_DEBUG, "Content written successfully to %s", writefile);

    	// Close syslog
    	closelog();
    	return 0;
}
