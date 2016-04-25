#include <stdio.h>   /* Standard input/output definitions */
#include <string.h>  /* String function definitions */
#include <unistd.h>  /* UNIX standard function definitions */
#include <fcntl.h>   /* File control definitions */
#include <errno.h>   /* Error number definitions */
#include <termios.h> /* POSIX terminal control definitions */
#include <time.h>
#include <stdlib.h>
#include <pthread.h>

#include <sqlite3.h>

// daemon requirements
#include <signal.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>

#define USE_MYSQL

#ifdef USE_MYSQL
#include <mysql/mysql.h>
#endif

#define HOMEDIR "/home/pi/Ciseco/monitor"
#define DATABASE "/home/pi/Ciseco/monitor/temperatures.db"

// Macros so we can either print to stdout or to the system log (if running as a daemon)
bool is_daemon = false;

#define LogDebug(...) if (is_daemon) syslog(LOG_DAEMON|LOG_DEBUG, __VA_ARGS__); else printf(__VA_ARGS__)
#define LogError(...) if (is_daemon) syslog(LOG_DAEMON|LOG_ERR, __VA_ARGS__); else printf(__VA_ARGS__)

struct SwitchDetails
{
	unsigned short sensor; // 2 chars cast as a short
	time_t last_on;
	time_t last_off;
	bool state; // on = true
};

struct SwitchAction
{
	unsigned short sensor; // 2 chars cast as a short
	char action[1024];
};

const int MaxSwitches = 5;
struct SwitchDetails Switches[MaxSwitches];
int nSwitches = 0;

struct SwitchAction SwitchActions[MaxSwitches];
int nActions = 0;

///////////////////////////////////////////////////////////////////////////////

int open_port(void)
{
	int fd; // file descriptor for the port

	fd = open("/dev/ttyAMA0", O_RDWR | O_NOCTTY | O_NDELAY);

	if (fd == -1)
	{
		LogError("open_port: Unable to open /dev/ttyAMA0");
	}
	else
	{
		//fcntl(fd, F_SETFL, 0);
		fcntl(fd, F_SETFL, FNDELAY); // return from reads immediately - no wait
	}

	return fd;
}

///////////////////////////////////////////////////////////////////////////////

int configure_port(int fd)
{
	struct termios options;

	// get current options

	tcgetattr(fd, &options);
	
	// set the baud rates to 9600
	
	cfsetispeed(&options, B9600);
	cfsetospeed(&options, B9600);

	options.c_cflag &= ~PARENB; // clear all parity gubbins
	options.c_cflag &= ~CSTOPB;
	options.c_cflag &= ~CSIZE; // mask the character size bits
	options.c_cflag |= CS8;    // select 8 data bits
	
	options.c_cflag |= (CLOCAL | CREAD); // enable the receiver and set local mode
	options.c_oflag &= ~OPOST; // raw output

	// raw input (not canonical)
	options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

	// set the new options for the port...

	tcsetattr(fd, TCSANOW, &options);
}

bool serial_write(int fd, const char* msg)
{
	return write(fd, msg, strlen(msg)) < 0; 
}


///////////////////////////////////////////////////////////////////////////////
// sqlite database stuff
///////////////////////////////////////////////////////////////////////////////

static sqlite3* Database = NULL; // global so we don't have to pass it to each fn

static int callback(void *NotUsed, int argc, char **argv, char **azColName)
{
	int i;
	for (i = 0; i<argc; i++)
	{
		LogDebug("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
	}
	LogDebug("\n");
	return 0;
}

bool store_temperature(const char* sensor, float temp)
{
	char  sql[128];
	char* errMsg;
	const int MaxRetries = 3;
	bool written = false;
	int rc = -1;
	
	for (int i = 0; i < MaxRetries && !written; ++i)
	{
		sprintf(sql, "INSERT INTO temps (timestamp, sensor, temp) VALUES (datetime('now'), '%s', %.1f);", sensor, temp);

		rc = sqlite3_exec(Database, sql, callback, 0, &errMsg);

		if (rc == SQLITE_OK)
		{
			written = true;
		}
		else
		{
			LogError("Error: unable to write temperature to DB (attempt %d) - %s\n", i+1, errMsg);
			sqlite3_free(errMsg);
			usleep(300000);
		}
	}

	return rc == SQLITE_OK;
}

bool store_battery(const char* sensor, float volts)
{
	char  sql[128];
	char* errMsg;

	sprintf(sql, "INSERT INTO batteries (timestamp, sensor, volts) VALUES (datetime('now'), '%s', %f);", sensor, volts);

	int rc = sqlite3_exec(Database, sql, callback, 0, &errMsg);

	if (rc != SQLITE_OK)
	{
		LogError("Error: unable to write battery voltage to DB - %s\n", errMsg);
		sqlite3_free(errMsg);
	}

	return rc == SQLITE_OK;

}

///////////////////////////////////////////////////////////////////////////////
// MySQL database stuff
///////////////////////////////////////////////////////////////////////////////

#ifdef USE_MYSQL

#define MYSQL_HOST "192.168.1.20" // NUC
#define MYSQL_USER "root"
#define MYSQL_PASSWORD "rootroot"
#define MYSQL_DB "aruba"

MYSQL MysqlDB;

bool open_database_mysql()
{
	if (!mysql_init(&MysqlDB))
		return false;

	bool arg = true;
	mysql_options(&MysqlDB, MYSQL_OPT_RECONNECT, &arg);

	if (!mysql_real_connect(&MysqlDB, MYSQL_HOST, MYSQL_USER, MYSQL_PASSWORD, MYSQL_DB, 3306, NULL, 0))
	{
		mysql_close(&MysqlDB);
		return false;
	}

	return true;
}

bool store_temperature_mysql(const char* sensor, float temp)
{
	char sql[128];
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	const int MaxTimeBuff = 30;
	char time_buff[MaxTimeBuff];
	strftime(time_buff, MaxTimeBuff, "%F %T", &tm); // %F = %Y-&m-%d %T = %H:%M:%S 

	sprintf(sql, "INSERT INTO temps (timestamp, sensor, temp) VALUES ('%s', '%s', %.1f);", time_buff, sensor, temp);

	if (mysql_query(&MysqlDB, sql))
	{
		LogError("Error: unable to write temperature to DB - %s\n", mysql_error(&MysqlDB));
		return false;
	}

	return true;
}

bool store_battery_mysql(const char* sensor, float volts)
{
	char  sql[128];
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	const int MaxTimeBuff = 30;
	char time_buff[MaxTimeBuff];
	strftime(time_buff, MaxTimeBuff, "%F %T", &tm); // %F = %Y-&m-%d %T = %H:%M:%S 

	sprintf(sql, "INSERT INTO batteries (timestamp, sensor, volts) VALUES ('%s', '%s', %f);", time_buff, sensor, volts);

	if (mysql_query(&MysqlDB, sql))
	{
		LogError("Error: unable to write battery voltage to DB - %s\n", mysql_error(&MysqlDB));
		return false;
	}

	return true;
}

#endif

///////////////////////////////////////////////////////////////////////////////
// Functions called when a switch changes state

int find_switch(const char* sensor)
{
	int index = -1;

	for (int i = 0; i < nSwitches && index == -1; i++)
	{
		if (Switches[i].sensor == *(unsigned short*)sensor)
			index = i;
	}

	return index;
}

void add_switch(const char* sensor, time_t now, bool state)
{
	int index = nSwitches;

	Switches[index].sensor = *(unsigned short*)sensor;
	Switches[index].last_on = state ? now : 0;
	Switches[index].last_off = state ? 0 : now;

	nSwitches++;
}

void* action_thread(void* param)
{
	// sensor is encoded as a short as it's actually just 2 chars

	unsigned short sensor = (unsigned short)param;

	for (int i = 0; i < nActions; i++)
	{
		if (sensor == SwitchActions[i].sensor)
		{
			system(SwitchActions[i].action);
			LogDebug("Action: %s\n", SwitchActions[i].action);
			break;
		}
	}

}

void execute_action(unsigned short sensor)
{
	pthread_t thread;

	int ret = pthread_create(&thread, NULL, action_thread, (void *)sensor);
}

void switch_on(const char* sensor)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	bool trigger = false;

	// find the switch in the array or add a new entry

	int index = find_switch(sensor);

	if (index == -1 && nSwitches < MaxSwitches) // not found
	{
		index = nSwitches;
		add_switch(sensor, ts.tv_sec, true);
		trigger = true;
	}
	else
	{
		if (Switches[index].state == false)
		{			
			if ((ts.tv_sec - Switches[index].last_on) > 5) // max rate once every 5s
				trigger = true;

			Switches[index].last_on = ts.tv_sec;
			Switches[index].state = true;
		}
	}

	if (trigger)
	{
		// do associated action

		execute_action(Switches[index].sensor);

		LogDebug("Switch %s turned ON\n", sensor);
	}
}


void switch_off(const char* sensor)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	// find the switch in the array or add a new entry

	int index = find_switch(sensor);

	if (index == -1) // not found
	{
		add_switch(sensor, ts.tv_sec, false);
	}
	else
	{
		if (Switches[index].state == true)
		{
			Switches[index].last_off = ts.tv_sec;
			Switches[index].state = false;
		}
	}

	// currently don't do anything when switch goes off
}

// actions are stored in actions.conf in the form
// <sensor>=<command>
// e.g.
// SA=echo "test message" | mail -s "Switched On" someone@somewhere.com

void load_switch_actions()
{
	for (int i = 0; i < MaxSwitches; i++)
	{
		SwitchActions[i].sensor = 0;
		SwitchActions[i].action[0] = '\0';
	}

	FILE* fin = fopen("actions.conf", "r");

	const int MaxLineLen = 1024;

	char line[MaxLineLen];

	if (fin)
	{
		while (fgets(line, MaxLineLen, fin) && nActions < MaxSwitches)
		{
			if (strlen(line) > 3)
			{
				line[2] = '\0';
				SwitchActions[nActions].sensor = *(unsigned short*)line;
				strcpy(SwitchActions[nActions].action, &line[3]);
				nActions++;
			}
		}

		fclose(fin);
	}

	LogDebug("Loaded %d switch actions\n", nActions);
}

///////////////////////////////////////////////////////////////////////////////

// get_llap_parameter()
// take 'count' chars and turn into a null terminated string removing any '-'s
void get_llap_parameter(const char* chars, int count, char* param)
{
	int j = 0;
	for (int i = 0; i < count; i++)
	{
		if (chars[i] != '-' || i == 0) // allow - in first char (for temp)
		{
			param[j] = chars[i];
			j++;
		}
	}

	param[j] = '\0';
}

void handle_llap_message(const char* llap)
{
	char address[3]; // 2 chars + null
	char message[10];
	char parameter[10];

	/*
	char debug[13];
	memcpy(debug, llap, 12);
	debug[12] = '\0';
	LogDebug("llap_message: [%s]\n", debug);
	*/

	address[0] = llap[1];
	address[1] = llap[2];
	address[2] = '\0';

	memcpy(message, llap+3, 9);
	message[9] = '\0';

	//LogDebug("From: %s got message %s\n", address, message);

	if (strncmp(message, "HELLO", 5) == 0)
	{
		// PING basically
	}
	else if (strncmp(message, "STARTED", 7) == 0)
	{
		// send ACK------
	}
	else if (strncmp(message, "SLEEPING", 8) == 0)
	{
		// just put to sleep
	}
	else if (strncmp(message, "AWAKE", 5) == 0)
	{
		// just woken
	}
	else if (strncmp(message, "BATTLOW", 7) == 0)
	{
		// warn of low battery
	}
	else if (strncmp(message, "BATT", 4) == 0)
	{
		// battery voltage
		get_llap_parameter(message + 4, 5, parameter);
		//LogDebug("  battery: %sV\n", parameter);

#if defined USE_MYSQL
		store_battery_mysql(address, (float)atof(parameter));
#else
		store_battery(address, (float)atof(parameter));
#endif

	}
	else if (strncmp(message, "TEMP", 4) == 0 || strncmp(message, "TMPA", 4) == 0)
	{
		// temperature
		get_llap_parameter(message+4, 5, parameter);

#if defined USE_MYSQL
		store_temperature_mysql(address, (float)atof(parameter));
#else
		store_temperature(address, (float)atof(parameter));
#endif

	}
	else if (strncmp(message, "SWITCHON", 8) == 0)
	{
		switch_on(address);
	}
	else if (strncmp(message, "SWITCHOFF", 9) == 0)
	{
		switch_off(address);
	}
}


///////////////////////////////////////////////////////////////////////////////

static void skeleton_daemon()
{
	pid_t pid;

	printf("starting daemon...\n");

	/* Fork off the parent process */
	pid = fork();

	/* An error occurred */
	if (pid < 0)
		exit(EXIT_FAILURE);

	/* Success: Let the parent terminate */
	if (pid > 0)
		exit(EXIT_SUCCESS);

	/* On success: The child process becomes session leader */
	if (setsid() < 0)
		exit(EXIT_FAILURE);

	/* Catch, ignore and handle signals */
	//TODO: Implement a working signal handler */
	signal(SIGCHLD, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	/* Fork off for the second time*/
	pid = fork();

	/* An error occurred */
	if (pid < 0)
		exit(EXIT_FAILURE);

	/* Success: Let the parent terminate */
	if (pid > 0)
		exit(EXIT_SUCCESS);

	/* Set new file permissions */
	umask(0);

	/* Change the working directory to the root directory */
	/* or another appropriated directory */
	chdir(HOMEDIR);

	/* Close all open file descriptors */
	for (int x = sysconf(_SC_OPEN_MAX); x>0; x--)
	{
		close(x);
	}

	/* Open the log file */
	openlog("monitor_temp", LOG_PID, LOG_DAEMON);
}



///////////////////////////////////////////////////////////////////////////////
// main()
///////////////////////////////////////////////////////////////////////////////


int main(int argc, char* argv[])
{
	size_t charsRead = 0;
	const int LlapMessageSize = 12;
	const int MaxBuffSize = LlapMessageSize * 2;
	char buff[MaxBuffSize];
	int buffCount = 0;

	struct timespec ts;
	time_t lastTimeReq = 0;

	if (argc > 1)
	{
		if (strncmp(argv[1], "-d", 2) == 0)
		{
			is_daemon = true;
			skeleton_daemon();
		}
		else
		{
			printf("%s -d to run as a daemon\n", argv[0]);
			return 1;
		}
	}

	// initialise switch array

	for (int i = 0; i < MaxSwitches; i++)
	{
		Switches[i].sensor = 0;
		Switches[i].last_on = 0;
		Switches[i].last_off = 0;
		Switches[i].state = false;
	}

	load_switch_actions();

	// open the serial port

	int fd = open_port();

	if (fd == -1)
		return 1;

	// open the database

#if defined USE_MYSQL
	
	LogDebug("Writing to MySQL DB\n");

	if (!open_database_mysql())
	{
		LogError("Error opening MySQL database\n");
	}

#else

	LogDebug("Writing to SQLite DB\n");

	int rc = sqlite3_open(DATABASE, &Database);

	if (rc != SQLITE_OK)
	{
		LogError("Error opening database\n");
		return 1;
	}

#endif

	LogDebug("Started...\n");

	char debug[50];

	int tempCount = 0;

	// main loop

	while (true)
	{

		/* Assume sensors are running in INTVL mode
		clock_gettime(CLOCK_MONOTONIC, &ts);

		if ((ts.tv_sec - lastTimeReq) >= 60) // 1 per min
		{
			lastTimeReq = ts.tv_sec;
			serial_write(fd, "aTATEMP-----");
			tempCount++;

			if (tempCount >= 10) // get the battery voltage every 10mins
			{
				serial_write(fd, "aTABATT-----");
				tempCount = 0;
			}

		}
		*/

		usleep(1000000);

		charsRead = read(fd, buff + buffCount, LlapMessageSize);

		if (charsRead > 0)
		{

	memcpy(debug, buff + buffCount, charsRead);
	debug[charsRead]='\0';
	LogDebug("Read: %d [%s]\n", charsRead, debug);

			buffCount += charsRead;

			// message format is 'aXXAAAAAAAAA'
			// where 'a' is the start XX is device address [A-Z][A-Z]
			//    and 'A...' is 12 chars padded with '-', if necessary

			if (buffCount >= LlapMessageSize)
			{
				// look for 'a'
				int startIndex = -1;
				for (int i = 0; i < buffCount && startIndex < 0; i++)
				{
					if (buff[i] == 'a')
						startIndex = i;
				}

				//LogDebug("startIndex: %d\n", startIndex);

				if (startIndex > -1)
				{
					if (startIndex > 0)
					{
						memmove(buff, buff + startIndex, buffCount - startIndex);
						buffCount -= startIndex;
					}

					// assume 1st 12 bytes are LLAP message
					handle_llap_message(buff); // note the message is NOT null terminated!

					// remove message from buffer
					memmove(buff, buff + LlapMessageSize, buffCount - LlapMessageSize);
					buffCount -= LlapMessageSize;
				}
			}
		}
	}

	close(fd);

#if defined USE_MYSQL
	mysql_close(&MysqlDB);
#else
	sqlite3_close(Database);
#endif
	return 0;
}