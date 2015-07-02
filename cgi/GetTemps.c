#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <string.h>
#include <string>
#include <list>

#define DATABASE "/home/pi/Ciseco/monitor/temperatures.db"

static const int MaxSensors = 3;

struct ChartPoint
{
	ChartPoint() { temp[0] = temp[1] = temp[2] = -9999.9; }
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
		sprintf(buff, ",\"timestamp\":\"%s\"", valid ?  timestamp.c_str() : "");
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
		/*json += ",\"chartData\":[";
json += "[\"Time\",\"Temperature\"]";

for (std::list<TempType>::const_iterator it = tempList.begin(); it != tempList.end(); ++it)
{
sprintf(buff, ",[\"%s\",%.1f]", (*it).timestamp.c_str(), (*it).temp);
json += buff;
}

json += "]}";
*/json += "}";
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

/*
struct ReturnObjectType
{
	ReturnObjectType() : valid(false), nSensors(0) {}
	std::string ToJson()
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

		json += "]";

		json += ",\"multilineChartData\":[";

		json += "[\"Time\"";
		for (int i = 0; i < nSensors; i++)
		{
			sprintf(buff, ",\"%s\"", sensors[i].location.c_str());
			json += buff;
		}
		json += "]";

		for (std::list<ChartPoint>::iterator it = chartList.begin(); it != chartList.end(); ++it)
		{
			json += ",";
			json += (*it).ToJson(nSensors);
		}

		json += "]";
		json += "}";

		return json;
	}

	bool valid; // have we got any data?

	SensorRecord sensors[MaxSensors];
	int nSensors;
	std::list<ChartPoint> chartList;
};
*/

// Global vars
//ReturnObjectType ReturnObject;
SensorTemperatures SensorTemps;
SensorChartData    ChartData;
int                SensorIndex = 0;

typedef enum {QUERY_MAX, QUERY_MIN, QUERY_AVG, QUERY_CURRENT, QUERY_TEMP_VALUES, QUERY_SENSORS, QUERY_CHART_LOCATIONS, QUERY_CHART_TIMES, QUERY_CHART_VALUES} QueryType;

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

/*	case QUERY_TEMP_VALUES:
		if (argc == 2 && argv[0] && argv[1])
		{
			TempType tempRecord;
			tempRecord.timestamp = argv[0];
			tempRecord.temp = (float)atof(argv[1]);
			ReturnObject.sensors[SensorIndex].tempList.push_back(tempRecord);
		}
		break;*/

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
	
	/*
	printf(">>> in callback:\n");
	for (int i = 0; i < argc; i++)
	{
		printf("col %s: %s\n", column[i], argv[i] ? argv[i] : "NULL");
	}
	*/

	return 0;
}

void return_error()
{
	//printf("Content-Type: text/plain;charset=us-ascii\n\n");
	//printf("Error: unable to open db\n\n");

	printf("Content-Type: application/json\n\n");
	printf("{\"valid\":false}\n");
}

//void return_json(void)
//{
//	printf("Content-Type: application/json\n\n");
//	printf(ReturnObject.ToJson().c_str());
//}

time_t GetTime()
{
	static time_t InitialS = 0;
	struct timespec spec;
	clock_gettime(CLOCK_REALTIME, &spec);

	if (InitialS == 0)
		InitialS = spec.tv_sec;

	return (spec.tv_sec - InitialS) * 1000 + spec.tv_nsec / 1000000;
}


bool SendTemperatures(sqlite3* db, int hours)
{
	char sql[512];
	char* errMsg;

	// find distinct sensors (and pair with their location description from the 'locations' table

	/* This version only returns sensors with data but is slow
	sprintf(sql, "SELECT sensors.sensor,locations.location FROM (SELECT DISTINCT sensor FROM temps WHERE datetime(timestamp) >= datetime('now', '-%d hours') ORDER BY sensor ASC) AS sensors INNER JOIN locations ON sensors.sensor = locations.sensor;", hours);

	if (sqlite3_exec(db, sql, db_callback, (void*)QUERY_SENSORS, &errMsg) != SQLITE_OK)
	{
		return_error();
		return false;
	}
	*/

	if (sqlite3_exec(db, "SELECT sensor, location FROM locations ORDER BY sensor ASC;", db_callback, (void*)QUERY_SENSORS, &errMsg) != SQLITE_OK)
	{
		return_error();
		return false;
	}

	// get temperatures for each sensor

	for (SensorIndex = 0; SensorIndex < SensorTemps.nSensors; ++SensorIndex)
	{
		sprintf(sql, "SELECT datetime(timestamp,'localtime'),MAX(temp) FROM temps WHERE sensor='%s' AND datetime(timestamp) >= datetime('now', '-%d hours');",
			SensorTemps.sensors[SensorIndex].name.c_str(), hours);

		if (sqlite3_exec(db, sql, db_callback, (void*)QUERY_MAX, &errMsg) != SQLITE_OK)
		{
			return_error();
			return false;
		}

		sprintf(sql, "SELECT datetime(timestamp,'localtime'),MIN(temp) FROM temps WHERE sensor='%s' AND datetime(timestamp) >= datetime('now', '-%d hours');",
			SensorTemps.sensors[SensorIndex].name.c_str(), hours);

		if (sqlite3_exec(db, sql, db_callback, (void*)QUERY_MIN, &errMsg) != SQLITE_OK)
		{
			return_error();
			return false;
		}

		/*
		sprintf(sql, "SELECT datetime(timestamp,'localtime'),AVG(temp) FROM temps WHERE sensor='%s' AND datetime(timestamp) >= datetime('now', '-%d hours');",
			SensorTemps.sensors[SensorIndex].name.c_str(), hours);

		if (sqlite3_exec(db, sql, db_callback, (void*)QUERY_AVG, &errMsg) != SQLITE_OK)
		{
			return_error();
			return false;
		}
		*/

		sprintf(sql, "SELECT datetime(timestamp,'localtime'),temp FROM temps WHERE sensor='%s' ORDER BY timestamp DESC LIMIT 1;",
			SensorTemps.sensors[SensorIndex].name.c_str());

		if (sqlite3_exec(db, sql, db_callback, (void*)QUERY_CURRENT, &errMsg) != SQLITE_OK)
		{
			return_error();
			return false;
		}
	}

	printf("Content-Type: application/json\n\n");
	printf(SensorTemps.ToJson().c_str());

	return true;
}

bool SendChartData(sqlite3* db, int hours)
{
	char sql[512];
	char* errMsg;

	// find distinct sensors (and pair with their location description from the 'locations' table

	sprintf(sql, "SELECT sensors.sensor,locations.location FROM (SELECT DISTINCT sensor FROM temps WHERE datetime(timestamp) >= datetime('now', '-%d hours') ORDER BY sensor ASC) AS sensors INNER JOIN locations ON sensors.sensor = locations.sensor;", 
		hours);

	if (sqlite3_exec(db, sql, db_callback, (void*)QUERY_CHART_LOCATIONS, &errMsg) != SQLITE_OK)
	{
		return_error();
		return false;
	}

	// before we enter the sensor loop, get distinct times for char x axis
	sprintf(sql, "SELECT DISTINCT strftime('%%Y-%%m-%%d %%H:%%M',timestamp,'localtime') FROM temps WHERE datetime(timestamp) >= datetime('now', '-%d hours');",
		hours);

	if (sqlite3_exec(db, sql, db_callback, (void*)QUERY_CHART_TIMES, &errMsg) != SQLITE_OK)
	{
		return_error();
		return false;
	}

	for (SensorIndex = 0; SensorIndex < ChartData.nSensors; ++SensorIndex)
	{
		sprintf(sql, "SELECT strftime('%%Y-%%m-%%d %%H:%%M',timestamp,'localtime'),temp from temps WHERE sensor='%s' AND datetime(timestamp) >= datetime('now','-%d hours');",
			ChartData.sensors[SensorIndex].c_str(), hours);

		if (sqlite3_exec(db, sql, db_callback, (void*)QUERY_CHART_VALUES, &errMsg) != SQLITE_OK)
		{
			return_error();
			return false;
		}
	}

	printf("Content-Type: application/json\n\n");
	printf(ChartData.ToJson().c_str());

	return true;
}




int main(int argc, char* argv[])
{
	sqlite3* db = NULL;
	//char* errMsg;

	// open the database
	if (sqlite3_open(DATABASE, &db) != SQLITE_OK)
	{
		// bail out if we can't
		return_error();
		return 1;
	}

	// find out how many hours we're interested in

	char* query_data = getenv("QUERY_STRING");
	int hours = 6;
	int dataType = 0;
	char sql[512];

	if (query_data && query_data[0] != '\0')
	{
		int h = 0;
		if (sscanf(query_data, "data=%d&hours=%d", &dataType, &h) == 2)
			hours = h;
	}

	if (dataType == 0)
		SendTemperatures(db, hours);
	else if (dataType == 1)
		SendChartData(db, hours);

	sqlite3_close(db);

	//// find distinct sensors (and pair with their location description from the 'locations' table

	//sprintf(sql, "SELECT sensors.sensor,locations.location FROM (SELECT DISTINCT sensor FROM temps WHERE datetime(timestamp) >= datetime('now', '-%d hours') ORDER BY sensor ASC) AS sensors INNER JOIN locations ON sensors.sensor = locations.sensor;", hours);

	//if (sqlite3_exec(db, sql, db_callback, (void*)QUERY_SENSORS, &errMsg) != SQLITE_OK)
	//{
	//	return_error();
	//	sqlite3_close(db);
	//	return 1;
	//}

	//// before we enter the sensor loop, get distinct times for char x axis
	//sprintf(sql, "SELECT DISTINCT strftime('%%Y-%%m-%%d %%H:%%M',timestamp,'localtime') FROM temps WHERE datetime(timestamp) >= datetime('now', '-%d hours');",
	//	hours);

	//if (sqlite3_exec(db, sql, db_callback, (void*)QUERY_CHART_TIMES, &errMsg) != SQLITE_OK)
	//{
	//	return_error();
	//	sqlite3_close(db);
	//	return 1;
	//}


	//// get temperatures for each sensor

	//for (SensorIndex = 0; SensorIndex < ReturnObject.nSensors; ++SensorIndex)
	//{
	//	sprintf(sql, "SELECT datetime(timestamp,'localtime'),MAX(temp) FROM temps WHERE sensor='%s' AND datetime(timestamp) >= datetime('now', '-%d hours');", 
	//		ReturnObject.sensors[SensorIndex].name.c_str(), hours);

	//	if (sqlite3_exec(db, sql, db_callback, (void*)QUERY_MAX, &errMsg) != SQLITE_OK)
	//	{
	//		return_error();
	//		sqlite3_close(db);
	//		return 1;
	//	}

	//	sprintf(sql, "SELECT datetime(timestamp,'localtime'),MIN(temp) FROM temps WHERE sensor='%s' AND datetime(timestamp) >= datetime('now', '-%d hours');",
	//		ReturnObject.sensors[SensorIndex].name.c_str(), hours);

	//	if (sqlite3_exec(db, sql, db_callback, (void*)QUERY_MIN, &errMsg) != SQLITE_OK)
	//	{
	//		return_error();
	//		sqlite3_close(db);
	//		return 1;
	//	}

	//	sprintf(sql, "SELECT datetime(timestamp,'localtime'),AVG(temp) FROM temps WHERE sensor='%s' AND datetime(timestamp) >= datetime('now', '-%d hours');", 
	//		ReturnObject.sensors[SensorIndex].name.c_str(), hours);

	//	if (sqlite3_exec(db, sql, db_callback, (void*)QUERY_AVG, &errMsg) != SQLITE_OK)
	//	{
	//		return_error();
	//		sqlite3_close(db);
	//		return 1;
	//	}

	//	sprintf(sql, "SELECT datetime(timestamp,'localtime'),temp FROM temps WHERE sensor='%s' ORDER BY timestamp DESC LIMIT 1;",
	//		ReturnObject.sensors[SensorIndex].name.c_str());

	//	if (sqlite3_exec(db, sql, db_callback, (void*)QUERY_CURRENT, &errMsg) != SQLITE_OK)
	//	{
	//		return_error();
	//		sqlite3_close(db);
	//		return 1;
	//	}

	//	// empty the chart list before this query
	//	/*
	//	ReturnObject.sensors[SensorIndex].tempList.clear();

	//	sprintf(sql, "SELECT datetime(timestamp,'localtime'),temp from temps WHERE sensor='%s' AND datetime(timestamp) >= datetime('now','-%d hours') ORDER BY timestamp ASC;",
	//		ReturnObject.sensors[SensorIndex].name.c_str(), hours);

	//	if (sqlite3_exec(db, sql, db_callback, (void*)QUERY_TEMP_VALUES, &errMsg) != SQLITE_OK)
	//	{
	//		return_error();
	//		sqlite3_close(db);
	//		return 1;
	//	}
	//	*/

	//	// multi-line chart query

	//	sprintf(sql, "SELECT strftime('%%Y-%%m-%%d %%H:%%M',timestamp,'localtime'),temp from temps WHERE sensor='%s' AND datetime(timestamp) >= datetime('now','-%d hours');",
	//		ReturnObject.sensors[SensorIndex].name.c_str(), hours);

	//	if (sqlite3_exec(db, sql, db_callback, (void*)QUERY_CHART_VALUES, &errMsg) != SQLITE_OK)
	//	{
	//		return_error();
	//		sqlite3_close(db);
	//		return 1;
	//	}
	//}

	//return_json();

	sqlite3_close(db);

	return 0;
}