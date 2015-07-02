// GetTemps.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <list>

#include <WinSock2.h>
#include <mysql.h>

static const int MaxSensors = 3;

struct ChartPoint
{
	ChartPoint() { temp[0] = temp[1] = temp[2] = -9999.9f; }
	std::string ToJson(int nSensors)
	{
		std::string json = "";
		char buff[128];

		sprintf(buff, "[\"%s\"", timestamp.c_str());
		json += buff;

		for (int i = 0; i < nSensors; i++)
		{
			if (temp[i] < -9999.0)
			{
				json += ",null";
			}
			else
			{
				sprintf(buff, ",%.1f", temp[i]);
				json += buff;
			}
		}

		json += "]";

		return json;
	}

	std::string timestamp; // this is only 'YYYY-MM-DD HH:MM' i.e. no seconds
	float temp[MaxSensors]; // null entry == -9999.9 do '< -9999.0' comparison since fp 
};

struct TempType
{
	TempType() : valid(false) {}
	std::string ToJson()
	{
		char buff[128];
		std::string json = "";
		sprintf(buff, "{\"valid\":%s", valid ? "true" : "false");
		json += buff;
		sprintf(buff, ",\"timestamp\":\"%s\"", valid ? timestamp.c_str() : "");
		json += buff;
		sprintf(buff, ",\"temp\":\"%.1f\"}", valid ? temp : 9999.0);
		json += buff;
		return json;
	}

	bool valid;
	std::string timestamp;
	float temp;
};

struct SensorRecord
{
	SensorRecord() : valid(false) {}

	std::string ToJson()
	{
		std::string json = "";
		char buff[1024];

		sprintf(buff, "{\"valid\":%s", valid ? "true" : "false");
		json += buff;

		sprintf(buff, ",\"name\":\"%s\"", name.c_str());
		json += buff;
		sprintf(buff, ",\"location\":\"%s\"", location.c_str());
		json += buff;
		sprintf(buff, ",\"currentTemp\":%s", currentTemp.ToJson().c_str());
		json += buff;
		sprintf(buff, ",\"minTemp\":%s", minTemp.ToJson().c_str());
		json += buff;
		sprintf(buff, ",\"maxTemp\":%s", maxTemp.ToJson().c_str());
		json += buff;
		sprintf(buff, ",\"avgTemp\":%s", avgTemp.ToJson().c_str());
		json += buff;

		json += "}";

		return json;
	}

	bool valid; // have we got any data?
	std::string name;
	std::string location;
	TempType currentTemp;
	TempType minTemp;
	TempType maxTemp;
	TempType avgTemp;
	std::list<TempType> tempList;
};

struct SensorTemperatures
{
	SensorTemperatures() : valid(false), nSensors(0) {}
	std::string ToJson(void)
	{
		char buff[256];
		std::string json = "";

		sprintf(buff, "{\"valid\":%s,\"sensors\": [", valid ? "true" : "false");

		json += buff;

		for (int i = 0; i < nSensors; i++)
		{
			if (sensors[i].valid)
			{
				if (i != 0) json += ",";

				json += sensors[i].ToJson();
			}
		}

		json += "]}";

		return json;
	}

	bool valid; // have we got any data?
	SensorRecord sensors[MaxSensors];
	int nSensors;
};

struct SensorChartData
{
	SensorChartData() : valid(false), nSensors(0) {}
	std::string ToJson()
	{
		char buff[256];
		std::string json = "";

		sprintf(buff, "{\"valid\":%s,\"chartData\":[", valid ? "true" : "false");
		json += buff;

		// 1st row contains the location names
		json += "[\"Time\"";
		for (int i = 0; i < nSensors; i++)
		{
			sprintf(buff, ",\"%s\"", locations[i].c_str());
			json += buff;
		}
		json += "]";

		for (std::list<ChartPoint>::iterator it = chartList.begin(); it != chartList.end(); ++it)
		{
			json += ",";
			json += (*it).ToJson(nSensors);
		}

		json += "]}";

		return json;
	}

	bool valid;
	std::string sensors[MaxSensors];
	std::string locations[MaxSensors];
	int nSensors;
	std::list<ChartPoint> chartList;
};

// Global vars

SensorTemperatures SensorTemps;
SensorChartData    ChartData;
int                SensorIndex = 0;


///////////////////////////////////////////////////////////////////////////////
// MySQL database stuff
///////////////////////////////////////////////////////////////////////////////

#define MYSQL_HOST "localhost" // NUC
#define MYSQL_USER "root"
#define MYSQL_PASSWORD "rootroot"
#define MYSQL_DB "aruba"

MYSQL MysqlDB;

bool OpenDatabase()
{
	if (!mysql_init(&MysqlDB))
	{
		return false;
	}

	bool arg = true;
	mysql_options(&MysqlDB, MYSQL_OPT_RECONNECT, &arg);

	if (!mysql_real_connect(&MysqlDB, MYSQL_HOST, MYSQL_USER, MYSQL_PASSWORD, MYSQL_DB, 3306, NULL, 0))
	{
		mysql_close(&MysqlDB);
		return false;
	}

	return true;
}

/*
typedef enum { QUERY_MAX, QUERY_MIN, QUERY_AVG, QUERY_CURRENT, QUERY_SENSORS, QUERY_CHART_LOCATIONS, QUERY_CHART_TIMES, QUERY_CHART_VALUES } QueryType;

static int db_callback(void *param, int argc, char **argv, char **column)
{
	switch ((int)param)
	{
	case QUERY_MIN:
		if (argc == 2 && argv[0] && argv[1])
		{
			SensorTemps.sensors[SensorIndex].minTemp.timestamp = argv[0];
			SensorTemps.sensors[SensorIndex].minTemp.temp = (float)atof(argv[1]);
			SensorTemps.sensors[SensorIndex].minTemp.valid = true;
			SensorTemps.sensors[SensorIndex].valid = true;
		}
		break;

	case QUERY_MAX:
		if (argc == 2 && argv[0] && argv[1])
		{
			SensorTemps.sensors[SensorIndex].maxTemp.timestamp = argv[0];
			SensorTemps.sensors[SensorIndex].maxTemp.temp = (float)atof(argv[1]);
			SensorTemps.sensors[SensorIndex].maxTemp.valid = true;
			SensorTemps.sensors[SensorIndex].valid = true;
		}
		break;

	case QUERY_AVG:
		if (argc == 2 && argv[0] && argv[1])
		{
			SensorTemps.sensors[SensorIndex].avgTemp.timestamp = argv[0];
			SensorTemps.sensors[SensorIndex].avgTemp.temp = (float)atof(argv[1]);
			SensorTemps.sensors[SensorIndex].avgTemp.valid = true;
			SensorTemps.sensors[SensorIndex].valid = true;
		}
		break;

	case QUERY_CURRENT:
		if (argc == 2 && argv[0] && argv[1])
		{
			SensorTemps.sensors[SensorIndex].currentTemp.timestamp = argv[0];
			SensorTemps.sensors[SensorIndex].currentTemp.temp = (float)atof(argv[1]);
			SensorTemps.sensors[SensorIndex].currentTemp.valid = true;
			SensorTemps.sensors[SensorIndex].valid = true;
		}
		break;

	case QUERY_SENSORS:
		if (argc == 2 && argv[0] && argv[1])
		{
			SensorTemps.sensors[SensorTemps.nSensors].name = argv[0];
			SensorTemps.sensors[SensorTemps.nSensors].location = argv[1];
			SensorTemps.nSensors++;
		}
		break;

	case QUERY_CHART_LOCATIONS:
		if (argc == 2 && argv[0] && argv[1])
		{
			ChartData.sensors[ChartData.nSensors] = argv[0];
			ChartData.locations[ChartData.nSensors] = argv[1];
			ChartData.nSensors++;
		}

	case QUERY_CHART_TIMES:
		if (argc == 1 && argv[0])
		{
			ChartPoint cp;
			cp.timestamp = argv[0];
			ChartData.chartList.push_back(cp);
		}
		break;

	case QUERY_CHART_VALUES:
		if (argc == 2 && argv[0] && argv[1])
		{
			for (std::list<ChartPoint>::iterator it = ChartData.chartList.begin(); it != ChartData.chartList.end(); ++it)
			{
				ChartPoint& cp = *it;
				if (cp.timestamp == argv[0]) // found the correct record so add temp to it
				{
					cp.temp[SensorIndex] = (float)atof(argv[1]);
					break;
				}
			}
		}
	}


	return 0;
}
*/

void ReturnError(const char* errMsg)
{
	printf("Content-Type: application/json\n\n");
	printf("{\"valid\":false,\"error\":\"%s\"}\n", errMsg);
}


bool SendTemperatures(int hours)
{
	char sql[512];
	MYSQL_RES* pResult;

	// find distinct sensors (and pair with their location description from the 'locations' table

	/* This version only returns sensors with data but is slow
	sprintf(sql, "SELECT sensors.sensor,locations.location FROM (SELECT DISTINCT sensor FROM temps WHERE datetime(timestamp) >= datetime('now', '-%d hours') ORDER BY sensor ASC) AS sensors INNER JOIN locations ON sensors.sensor = locations.sensor;", hours);

	if (sqlite3_exec(db, sql, db_callback, (void*)QUERY_SENSORS, &errMsg) != SQLITE_OK)
	{
	return_error();
	return false;
	}
	*/

	if (mysql_query(&MysqlDB, "SELECT sensor, location FROM locations ORDER BY sensor ASC"))
	{
		ReturnError("Unable to read locations");
		return false;
	}

	pResult = mysql_store_result(&MysqlDB);
	if (pResult)
	{
		MYSQL_ROW row;
		while (row = mysql_fetch_row(pResult))
		{
			if (row[0] && row[1])
			{
				SensorTemps.sensors[SensorTemps.nSensors].name = row[0];
				SensorTemps.sensors[SensorTemps.nSensors].location = row[1];
				SensorTemps.nSensors++;
			}
		}
		mysql_free_result(pResult);
	}

	// get temperatures for each sensor

	for (SensorIndex = 0; SensorIndex < SensorTemps.nSensors; ++SensorIndex)
	{
		// find maximum temperature over the past n hours

		sprintf(sql, "SELECT timestamp,MAX(temp) FROM temps WHERE sensor='%s' AND timestamp >= DATE_SUB(NOW(), INTERVAL %d HOUR)",
			SensorTemps.sensors[SensorIndex].name.c_str(), hours);

		if (mysql_query(&MysqlDB, sql))
		{
			ReturnError("Unable to read MAX temperature");
			return false;
		}

		pResult = mysql_store_result(&MysqlDB);
		if (pResult)
		{
			MYSQL_ROW row;
			while (row = mysql_fetch_row(pResult))
			{
				if (row[0] && row[1])
				{
					SensorTemps.sensors[SensorIndex].maxTemp.timestamp = row[0];
					SensorTemps.sensors[SensorIndex].maxTemp.temp = (float)atof(row[1]);
					SensorTemps.sensors[SensorIndex].maxTemp.valid = true;
					SensorTemps.sensors[SensorIndex].valid = true;
				}
			}
			mysql_free_result(pResult);
		}

		// find minimum temperature over the past n hours

		sprintf(sql, "SELECT timestamp,MIN(temp) FROM temps WHERE sensor='%s' AND timestamp >= DATE_SUB(NOW(), INTERVAL %d HOUR)",
			SensorTemps.sensors[SensorIndex].name.c_str(), hours);

		if (mysql_query(&MysqlDB, sql))
		{
			ReturnError("Unable to MIN temperature");
			return false;
		}

		pResult = mysql_store_result(&MysqlDB);
		if (pResult)
		{
			MYSQL_ROW row;
			while (row = mysql_fetch_row(pResult))
			{
				if (row[0] && row[1])
				{
					SensorTemps.sensors[SensorIndex].minTemp.timestamp = row[0];
					SensorTemps.sensors[SensorIndex].minTemp.temp = (float)atof(row[1]);
					SensorTemps.sensors[SensorIndex].minTemp.valid = true;
					SensorTemps.sensors[SensorIndex].valid = true;
				}
			}
			mysql_free_result(pResult);
		}

		////////////////////////////////////////////////////////////////
		// Do AVG if DB is fast enough
		///////////////////////////////////////////////////////////////

		sprintf(sql, "SELECT timestamp,temp FROM temps WHERE sensor='%s' ORDER BY timestamp DESC LIMIT 1",
			SensorTemps.sensors[SensorIndex].name.c_str(), hours);

		if (mysql_query(&MysqlDB, sql))
		{
			ReturnError("Unable to MIN temperature");
			return false;
		}

		pResult = mysql_store_result(&MysqlDB);
		if (pResult)
		{
			MYSQL_ROW row = mysql_fetch_row(pResult);
			if (row && row[0] && row[1])
			{
				SensorTemps.sensors[SensorIndex].currentTemp.timestamp = row[0];
				SensorTemps.sensors[SensorIndex].currentTemp.temp = (float)atof(row[1]);
				SensorTemps.sensors[SensorIndex].currentTemp.valid = true;
				SensorTemps.sensors[SensorIndex].valid = true;
			}
			mysql_free_result(pResult);
		}

	}

	printf("Content-Type: application/json\n\n");
	printf(SensorTemps.ToJson().c_str());

	return true;
}

bool SendChartData(int hours)
{
	char sql[512];
	MYSQL_RES* pResult;

	// find distinct sensors (and pair with their location description from the 'locations' table

	sprintf(sql, "SELECT sensors.sensor,locations.location FROM (SELECT DISTINCT sensor FROM temps WHERE timestamp >= DATE_SUB(NOW(), INTERVAL %d HOUR) ORDER BY sensor ASC) AS sensors INNER JOIN locations ON sensors.sensor = locations.sensor",
		hours);

	//"SELECT sensor, location FROM locations ORDER BY sensor ASC"

	if (mysql_query(&MysqlDB, sql))
	{
		ReturnError("Unable to read chart data locations");
		return false;
	}

	pResult = mysql_store_result(&MysqlDB);
	if (pResult)
	{
		MYSQL_ROW row;
		while (row = mysql_fetch_row(pResult))
		{
			if (row[0] && row[1])
			{
				ChartData.sensors[ChartData.nSensors] = row[0];
				ChartData.locations[ChartData.nSensors] = row[1];
				ChartData.nSensors++;
			}
		}
		mysql_free_result(pResult);
	}

	// before we enter the sensor loop, get distinct times for char x axis
	sprintf(sql, "SELECT DISTINCT DATE_FORMAT(timestamp, '%%Y-%%m-%%d %%H:%%i') FROM temps WHERE timestamp >= DATE_SUB(NOW(), INTERVAL %d HOUR)",
		hours);

	if (mysql_query(&MysqlDB, sql))
	{
		ReturnError("Unable to read chart data times");
		return false;
	}

	pResult = mysql_store_result(&MysqlDB);
	if (pResult)
	{
		MYSQL_ROW row;
		while (row = mysql_fetch_row(pResult))
		{
			if (row[0])
			{
				ChartPoint cp;
				cp.timestamp = row[0];
				ChartData.chartList.push_back(cp);
			}
		}
		mysql_free_result(pResult);
	}

	// now get the temperature sets for each sensor

	for (SensorIndex = 0; SensorIndex < ChartData.nSensors; ++SensorIndex)
	{
		sprintf(sql, "SELECT DATE_FORMAT(timestamp, '%%Y-%%m-%%d %%H:%%i'),temp from temps WHERE sensor='%s' AND timestamp >= DATE_SUB(NOW(), INTERVAL %d HOUR)",
			ChartData.sensors[SensorIndex].c_str(), hours);

		if (mysql_query(&MysqlDB, sql))
		{
			ReturnError("Unable to read chart data values");
			return false;
		}

		pResult = mysql_store_result(&MysqlDB);
		if (pResult)
		{
			MYSQL_ROW row;
			while (row = mysql_fetch_row(pResult))
			{
				if (row[0] && row[1])
				{
					for (std::list<ChartPoint>::iterator it = ChartData.chartList.begin(); it != ChartData.chartList.end(); ++it)
					{
						ChartPoint& cp = *it;
						if (cp.timestamp == row[0]) // found the correct record so add temp to it
						{
							cp.temp[SensorIndex] = (float)atof(row[1]);
							break;
						}
					}
				}
			}
			mysql_free_result(pResult);
		}
	}

	printf("Content-Type: application/json\n\n");
	printf(ChartData.ToJson().c_str());

	return true;
}



///////////////////////////////////////////////////////////////////////////////
// main() 
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
	
	if (!OpenDatabase())
	{
		ReturnError("Unable to open database\n");
		return 1;
	}

	// find out how many hours we're interested in
	const DWORD MaxQueryString = 128;
	char queryString[MaxQueryString];
	DWORD qsLen = GetEnvironmentVariable("QUERY_STRING", queryString, MaxQueryString);

	int hours = 6;
	int dataType = 0;

	if (qsLen > 0 && queryString[0] != '\0')
	{
		int h = 0;
		if (sscanf(queryString, "data=%d&hours=%d", &dataType, &h) == 2)
			hours = h;
	}

	if (dataType == 0)
		SendTemperatures(hours);
	else if (dataType == 1)
		SendChartData(hours);

	mysql_close(&MysqlDB);

	return 0;
}