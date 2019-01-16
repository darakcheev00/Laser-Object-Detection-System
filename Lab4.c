#include "gpiolib_addr.h"
#include "gpiolib_reg.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/watchdog.h> 	//needed for the watchdog specific constants
#include <sys/ioctl.h> 		//needed for the ioctl function
#include <sys/time.h>           //for gettimeofday()

// Daniel Dobre, Umar Hussain, Daniel Arakcheev


#define PRINT_MSG(file, time, programName, str) \
	do{ \
			fprintf(logFile, "%s : %s : %s", time, programName, str); \
			fflush(logFile); \
	}while(0)

#define PRINT_STAT(file, time, str, laser1Count, laser2Count, numberIn, numberOut) \
	do{ \
			fprintf(statsFile, "%s, %s : L1Count : %d : L2Count : %d : Num In : %d : Num OUT: %d \n \n", time, str, laser1Count, laser2Count, numberIn, numberOut); \
			fflush(statsFile); \
	}while(0)


//HARDWARE DEPENDENT CODE BELOW
#ifndef MARMOSET_TESTING

/* You may want to create helper functions for the Hardware Dependent functions*/

//This function should initialize the GPIO pins
GPIO_Handle initializeGPIO()
{
	//This is the same initialization that was done in Lab 2
	GPIO_Handle gpio;
	gpio = gpiolib_init_gpio();
	if(gpio == NULL)
	{
		perror("Could not initialize GPIO");
	}
	return gpio;
}

//This function should accept the diode number (1 or 2) and output
//a 0 if the laser beam is not reaching the diode, a 1 if the laser
//beam is reaching the diode or -1 if an error occurs.
#define LASER1_PIN_NUM 4
#define LASER2_PIN_NUM 5
int laserDiodeStatus(GPIO_Handle gpio, int diodeNumber)
{
	if(gpio == NULL)
	{
		return -1;
	}

	
	if(diodeNumber == 1) //condition for diode 1
	{
		
		uint32_t level_reg = gpiolib_read_reg(gpio, GPLEV(0));

		if(level_reg & (1 << LASER1_PIN_NUM)) //if laser is shining on the diode, pin state = true
		{
			return 1;
		}
		else // if no light shining on diode, pin state = false
		{
			return 0;
		}
	}
	
	else if(diodeNumber == 2) //condition for diode 2
	{
		uint32_t level_reg = gpiolib_read_reg(gpio, GPLEV(0));

		if(level_reg & (1 << LASER2_PIN_NUM))
		{
			return 1;
		}
		else
		{
			return 0;
		}
		
	}
	
	else
	{
		return -1;
	}
	
	
}

// function that will call the diode status function multiple times and return the correct diode status after debouncing with hysteresis
int diodeStatusDebounce(GPIO_Handle gpio, int diodeNumber, int kMax)
{
	enum State { START, DONE, GOT_KTH_ONE, GOT_KTH_ZERO };
	enum State s = START;
	
	int diodeStatus = -1;	
	int k = 0;
	
	while(s != DONE)
	{
		int input = laserDiodeStatus(gpio, diodeNumber);
		
		switch (s)
		{
			case START:
				if(input == 0)
					s = GOT_KTH_ZERO;

				else if (input == 1)
					s = GOT_KTH_ONE;

				break;
				
			case GOT_KTH_ZERO:
				if (k > kMax)
				{
					diodeStatus = 0;
					s = DONE;
				}
				else if (input == 0)
					s = GOT_KTH_ZERO;

				else if (input == 1)
				{
					s = GOT_KTH_ONE;
					k = 0;
				}
				break;
				
			case GOT_KTH_ONE:
				if (k > kMax)
				{
					diodeStatus = 1;
					s = DONE;
				}
				else if (input == 1)
					s = GOT_KTH_ONE;

				else if (input == 0)
				{
					s = GOT_KTH_ZERO;
					k = 0;
				}
				break;
				
			case DONE:
				
			default:
				return -1;
				break;			
		}
		
		
		switch (s) // actions for each state
		{
			case START:
			case GOT_KTH_ZERO:
				k++;
				break;
			case GOT_KTH_ONE:
				k++;
				break;
			case DONE:
				// fprintf(stderr, "DIODESTATUS RETURNED\n");
				return diodeStatus;
			default:
				return -1;
				break;
		}
		
		
		usleep(1000); // sleep for one ms each time through, to reduce bouncing
	}
	
	return -1;
}


#endif
//END OF HARDWARE DEPENDENT CODE

void getTime(char* buffer)
{
	//Create a timeval struct named tv
  	struct timeval tv;

	//Create a time_t variable named curtime
  	time_t curtime;


	//Get the current time and store it in the tv struct
  	gettimeofday(&tv, NULL); 

	//Set curtime to be equal to the number of seconds in tv
  	curtime=tv.tv_sec;

	//This will set buffer to be equal to a string that in
	//equivalent to the current date, in a month, day, year and
	//the current time in 24 hour notation.
  	strftime(buffer,30,"%m-%d-%Y  %T.",localtime(&curtime));

}


// state machine based function to read the contents of a config file. This assumes the config file will contain 4 parameters, a WD timeout, a logFile name, a statFile name
// and the amount of time to run for
// the state machine will get each line of the file, and save a temporary parameter and value string if they exists. It will then compare the parameter with any of the required
// ones and if it matches will set the value equal
void readConfig(FILE* configFile, int* timeout, char* logFileName, char* statsFileName)
{
	enum State { START, WS, COMMENT, EQUAL, PARAMETER, VALUE, DONE };
	enum State s = START;

	char buffer[255];
	*timeout = 0;

	

	while (fgets(buffer, 255, configFile) != NULL) //  this loop will go through the entire fill line by line
	{

		int i = 0;
		
		char Param[50];
		char Value[50];
		int p = 0; // counter for parameter string
		int v = 0; // counter for buffer string
        s = START;
        
        printf("Line read %s\n", buffer);
        
		for (int i = 0; i < 50; i++) // zero fill the param and value strings
		{
			Param[i] = 0;
			Value[i] = 0;
		}

		while (s != DONE)
		{ // for each line, this loop will get the parameter and value (if it exists)

         if(buffer == NULL)
             s = DONE;
            
		char currIn = buffer[i];
        
        printf("curr in is %c\n", currIn);
            
		switch (s) 
		{
		//===========================sudo======================
		case START:
			if (currIn == '#')
				s = DONE;
			else if ((currIn >= 'A' && currIn <= 'Z') || (currIn >= 'a' && currIn <= 'z')) // got a param
			{
                s = PARAMETER;
				Param[p] = currIn;
				i++;
				p++;
			}
			else if (currIn == ' ' || currIn == '\t') 
			{
				s = WS;
				i++;
			}
            else
                s = DONE;
			break;
		//=================================================
		case COMMENT:
			s = DONE;
			break;

		//========================ONE WHITE SPACE CASE===========
		case WS:
                printf("Current state is WS \n");
			//for another space or tab
			if(currIn == ' ' || currIn=='\t')
			{
				s = WS;
				i++;
			}
            
            else if(currIn == '=')
            {
                s = EQUAL;
                i++;
            }  
                
			//for a letter, means it got a parameter as input
		  	else if((currIn >= 'A' && currIn <= 'Z') || (currIn >= 'a' && currIn <= 'z'))
			{
				Param[p] = currIn;
				s = PARAMETER;
				i++;
				p++;
			}
                
			//for a number or slash, since for our values, they must start with either a number or slash
			else if((currIn >= '0' && currIn <= '9') || currIn == '/')
			{
				Value[v] = currIn;
				s = VALUE;
				i++;
				v++;
			}
		  
			
		break;		
			
		//=================================================             
		case PARAMETER:
                printf("Current state is PARAM \n");
			if(currIn == ' ' || currIn=='\t')
			{
			   	s = WS;
			   	i++;
			}
			   
			else if(currIn == '=')
			{
			   	s = EQUAL;
			   	i++;
			}
			else
			{
			   	Param[p] = currIn;
			   	i++;
			   	p++;
			}
			break;
		//=================================================
		case EQUAL:
			//for another space or tab
            printf("Current state is EQUAL \n");
                
			if(currIn == ' ' || currIn == '\t')
			{
				s = WS;
				i++;
			}
			//for a number or slash
			/*if((currIn >= '0' && currIn <= '9')|| currIn >= '/'){
			  	if (Param[0]=='W')
				  *timeout = (*timeout *10) + (currIn - '0');
			  	else
				  *runTime = (*runTime *10) + (currIn - '0');*/
			else if(currIn == '#')
			   s = DONE;
			  
			else
			{
				Value[v] = currIn;
				s = VALUE;
				i++;
				v++;
			}
			break;
			   
		//=================================================
		case VALUE: // if the next character is also a character and not a comment or whitespace, means it is still part of the value
                printf("Current state is VALUE \n");
			if (currIn != 0 && currIn != ' ' && currIn !='\t' && currIn != '#')
            {
				
			  s=VALUE;
			  Value[v] = currIn;
			  i++;
			  v++;
			  break;
			}
		else
			s= DONE;
			
		//=================================================     
		case DONE: // do we need this is other cases are calling s = DONE to break the loop?
			break;
         
         default:
             return;
             break;
	    
					
			}
		}
        //printf("exited SM \n", Param[0], Value[0]);
        
	// compare the parameter value to the desired ones to see if it matches
	//parameter values to compare against
		char timoutCHR[] = "WATCHDOG_TIMEOUT";
		char logfileCHR[] = "LOGFILE";
		char statsfileCHR[] = "STATSFILE";
				
		int isTime = 0;
		int isLog = 0;
		int isStat = 0;
				
	
		for(int i = 0; i <= 8; i++) // compare the first five character of our param with the desired parameter inputs
		{
			if(Param[i] == 	timoutCHR[i])
			   isTime = 1;

			 else
			   isTime = 0;					
		}
	
		for(int i = 0; i <= 5; i++) // compare the first five character of our param with the desired parameter inputs
		{
			if(Param[i] == 	logfileCHR[i])
			   isLog = 1;

			 else
			   isLog = 0;					
		}
	
		for(int i = 0; i <= 8; i++) // compare the first five character of our param with the desired parameter inputs
		{
			if(Param[i] == 	statsfileCHR[i])
			   isStat = 1;

			 else
			   isStat = 0;					
		}
			   
		if(isTime)
		   {
		   		*timeout += (Value[0]-'0');
            if(*timeout == 1 && (Value[1] >= '0' && Value[1] <= '9'))
            {
                *timeout *= 10;
                *timeout += (Value[1] - '0');
            }
		   }
	
		else if(isLog)
		   {
			for(int i = 0; Value[i] != 0; i++)
		   		logFileName[i] = Value[i];
		   }
	
		else if(isStat)
		   {
			for(int i = 0; Value[i] != 0; i++)
		   		statsFileName[i] = Value[i];
		   }
		   


	}
}


//This function will output the number of times each laser was broken
//and it will output how many objects have moved into and out of the room.

//laser1Count will be how many times laser 1 is broken (the left laser).
//laser2Count will be how many times laser 2 is broken (the right laser).
//numberIn will be the number  of objects that moved into the room.
//numberOut will be the number of objects that moved out of the room.
void outputMessage(int laser1Count, int laser2Count, int numberIn, int numberOut)
{
	printf("Laser 1 was broken %d times \n", laser1Count);
	printf("Laser 2 was broken %d times \n", laser2Count);
	printf("%d objects entered the room \n", numberIn);
	printf("%d objects exitted the room \n", numberOut);
}

//This function accepts an errorCode. You can define what the corresponding error code
//will be for each type of error that may occur.
void errorMessage(int errorCode)
{
	fprintf(stderr, "An error occured; the error code was %d \n", errorCode);
}

//State machine function to determine how many times each laser was broken, as well as how many objects entered and exited
//over the given time interval.
enum State { START, DONE, UNBROKEN, L1_BROKEN, L2_BROKEN, L12_BROKEN, L21_BROKEN, IN, OUT };
int countLaserBreaks(GPIO_Handle gpio, const int kMax, int *laser1Count, int *laser2Count, int *numIn, int *numOut, FILE* logFile, FILE* statsFile, char* programName, int watchdog)
{
	
	enum State s = START;
	
	*laser1Count = 0;
	*laser2Count = 0;
	*numIn = 0;
	*numOut = 0;
	
	if(kMax < 0)
	{	
		errorMessage(1);
		return -1;
	}
	
	time_t startTime = time(NULL);
	time_t currentTime = time(NULL);
	time_t statsTime = time(NULL);
	
	while(1)
	{
		//if((time(NULL) - startTime) >=  timeLimit)
			//s = DONE;
		
		// kick the watchdog after every 2 seconds of running the loop
		if((time(NULL) - currentTime) >= 2)
		{
		ioctl(watchdog, WDIOC_KEEPALIVE, 0);
		
		char timeSTR[30];
		getTime(timeSTR);
		//Log that the Watchdog was kicked
		PRINT_MSG(logFile, timeSTR, programName, "The Watchdog was kicked\n\n");
			
		// set the current time
		currentTime = time(NULL);
		}
		
		
		int diode1State = diodeStatusDebounce(gpio, 1, kMax);
		int diode2State = diodeStatusDebounce(gpio, 2, kMax);
			
		//printf("Through loop \n");
		
	if(diode1State == -1 || diode2State == -1)
		{// add error msg
			return -1;
		}
		
		switch (s) // Transitions for each state
		{ // diodestate is 0 if the laser is broken, 1 if it is reaching
			case START:
				if(diode1State && diode2State) // assuming lasers are both reaching on start
					s = UNBROKEN;
				break;
				
			case UNBROKEN: // Both laseres are unbroken
				//printf("UNBROKEN \n");
				if(!diode1State)
				{
					s = L1_BROKEN;
					(*laser1Count)++;
				}
				else if(!diode2State)
				{
					s = L2_BROKEN;
					(*laser2Count)++;
				}
				else
					s = UNBROKEN;
				break;
				
			case L1_BROKEN: // Both laseres were unbroken then Laser 1 was broken
				//printf("L1_BROKEN \n");
				if(diode1State)
					s = UNBROKEN;
				else if(!diode2State)
				{
					s = L12_BROKEN;
					(*laser2Count)++;
				}
				else
					s = L1_BROKEN;
				break;
				
			case L2_BROKEN: // Both laseres were unbroken then Laser 1 was broken
				//printf("l2_BROKEN \n");
				if(diode2State) // laser 2 becomes unbroken again
					s = UNBROKEN;
				else if(!diode1State) // laser 1 becomes broken (both 1 and 2 are broken)
				{
					s = L21_BROKEN;
					(*laser1Count)++;
				}
				else 
					s = L2_BROKEN;
				break;
				
			case L12_BROKEN: // Both lasers were broken
				//printf("L12_BROKEN \n");
				if(diode1State && !diode2State) // both laseres were broken and then laser 1 was unbroken, object MAY be entering
					s = IN;
				else if(!diode1State && diode2State) // if breaks both lasers then does not go fully in but instead starts reversing (2 becomes unbroken)
					s = L1_BROKEN;
				else 
					L12_BROKEN;
				break;
			
			case L21_BROKEN: // both lasers were broken
				//printf("L21_BROKEN \n");
				if(!diode1State && diode2State) // both laseres were broken and then laser 2 was unbroken, object MAY be exiting
					s = OUT;
				else if(diode1State && !diode2State) // if breaks both lasers then does not go fully in but instead starts reversing (laser 1 becomes unbroken again)
					s = L2_BROKEN;
				else 
					s = L21_BROKEN;
				break;
				
			case IN: // Laser 1 is now unbroken while 2 remains broken 
				//printf("IN \n");
				if(diode1State && diode2State) // both lasers are now unbroken, object has entered
				{
					(*numIn)++;
					s = UNBROKEN;
				}
				else if(!diode1State && !diode2State) //object reveresed back in and now both laseres are again broken
				{
					(*laser1Count)++;
					s = L12_BROKEN;
				}
				else
					s = IN;
				break;
				
			case OUT: // Laser 2 is now unbroken while 1 remains broken
				//printf("OUT\n");
				if(diode1State && diode2State) // both lasers are now unbroken, object has exited
				{
					(*numOut)++;
					s = UNBROKEN;
				}
				else if(!diode1State && !diode2State) //object reveresed back in and now both laseres are again broken
				{
					(*laser2Count)++;
					s = L21_BROKEN;
				}
				else
					s = OUT;
				break;
				
			case DONE:
				break;
				
			default:
				return -1;
				break;
				
		}
		
		
		usleep(5000);
		
		if((time(NULL) - statsTime) >= 10) // print a message to the statsfile every 10s
		{
		char timeSTR[30];
		getTime(timeSTR);
		PRINT_STAT(statsFile, timeSTR, "Stats recorded\n\n", *laser1Count, *laser2Count, *numIn, *numOut);
		PRINT_MSG(statsFile, timeSTR, programName, "Stats recorded\t");
			
		// set the current time
		statsTime = time(NULL);
		}
		
	}
	
	return 0;
	
}

#ifndef MARMOSET_TESTING

int main(const int argc, const char* const argv[])
{
	
	const char* argName = argv[0];

	//These variables will be used to count how long the name of the program is
	int i = 0;
	int namelength = 0;

	while(argName[i] != 0)
	{
		namelength++;
		i++;
	} 

	char programName[namelength];

	i = 0;

	//Copy the name of the program without the ./ at the start
	//of argv[0]
	while(argName[i + 2] != 0)
	{
		programName[i] = argName[i + 2];
		i++;
	} 	

	//Create a file pointer named configFile
	FILE* configFile;
	//Set configFile to point to the Lab4Sample.cfg file. It is
	//set to read the file.
	configFile = fopen("/home/pi/lasers.cfg", "r");

	//Output a warning message if the file cannot be openned
	if(!configFile)
	{
		perror("The config file could not be opened");
		return -1;
	}

	//Declare the variables that will be passed to the readConfig function
	int timeout;
	char logFileName[50];
	char statsFileName[50];
	
	//read the contents of the config file
	readConfig(configFile, &timeout, logFileName, statsFileName);
	
	
	FILE* logFile;
	//Set it to point to the file from the config file and make it append to
	//the file when it writes to it.
	logFile = fopen(logFileName, "a");
	
	//Check that the file opens properly.
	if(!logFile)
	{
		perror("The log file could not be opened");
		return -1;
	}
	
	FILE* statsFile;
	
	statsFile = fopen(statsFileName, "a");
	
	if(!statsFile)
	{
		perror("The stats file could not be opened");
		PRINT_MSG(logFile, time, programName, "The stats file could not be opend\n\n");
		return -1;
	}
	
	//Create a char array that will be used to hold the time values
	char time[30];
	getTime(time);
	
	
	
	int kMax = 5;
	int laser1Count = 0;
	int laser2Count = 0;
	int numIn = 0;
	int numOut = 0;
	
	// call function to initialize gpio
	GPIO_Handle gpio = initializeGPIO();
	
	getTime(time);
	//Log that the GPIO pins have been initialized
	PRINT_MSG(logFile, time, programName, "The GPIO pins have been initialized\n\n");
	
	
	//This variable will be used to access the /dev/watchdog file, similar to how
	//the GPIO_Handle works
	int watchdog;

	//We use the open function here to open the /dev/watchdog file. If it does
	//not open, then we output an error message. We do not use fopen() because we
	//do not want to create a file if it doesn't exist
	if ((watchdog = open("/dev/watchdog", O_RDWR | O_NOCTTY)) < 0) {
		printf("Error: Couldn't open watchdog device! %d\n", watchdog);
		return -1;
	} 
	//Get the current time
	getTime(time);
	//Log that the watchdog file has been opened
	PRINT_MSG(logFile, time, programName, "The Watchdog file has been opened\n\n");
	
	
	//This line uses the ioctl function to set the time limit of the watchdog
	ioctl(watchdog, WDIOC_SETTIMEOUT, &timeout);
	
	//Get the current time
	getTime(time);
	//Log that the Watchdog time limit has been set
	PRINT_MSG(logFile, time, programName, "The Watchdog time limit has been set\n\n");

	//The value of timeout will be changed to whatever the current time limit of the
	//watchdog timer is
	ioctl(watchdog, WDIOC_GETTIMEOUT, &timeout);

	//This print statement will confirm to us if the time limit has been properly
	//changed. The \n will create a newline character similar to what endl does.
	printf("The watchdog timeout is %d seconds.\n\n", timeout);
	
	
	
	getTime(time);
	PRINT_MSG(logFile, time, programName, "Starting laser couting......\n\n");
	
	int lasers = countLaserBreaks(gpio, kMax, &laser1Count, &laser2Count, &numIn, &numOut, logFile, statsFile, programName, watchdog);
	
	
	if( lasers < 0)
	{
		errorMessage(2);
		return -1;
	}
	
		
	else
		outputMessage(laser1Count, laser2Count, numIn, numOut);
	
	
	write(watchdog, "V", 1);
	getTime(time);
	//Log that the Watchdog was disabled
	PRINT_MSG(logFile, time, programName, "The Watchdog was disabled\n\n");

	//Close the watchdog file so that it is not accidentally tampered with
	close(watchdog);
	getTime(time);
	//Log that the Watchdog was closed
	PRINT_MSG(logFile, time, programName, "The Watchdog was closed\n\n");

	//Free the gpio pins
	gpiolib_free_gpio(gpio);
	getTime(time);
	//Log that the GPIO pins were freed
	PRINT_MSG(logFile, time, programName, "The GPIO pins have been freed\n\n");
	

	return 0;
	
	
}

#endif