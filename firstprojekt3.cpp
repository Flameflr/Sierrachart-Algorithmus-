#include "sierrachart.h"
#include <vector>
#include <algorithm>

SCDLLName("tops&bottoms_closedbars")

SCString msg;

struct top {
    int i;
    int tmp_max_i;
    float tmp_max;
	float tmp_close;
};

struct bottom {
    int i;
    int tmp_min_i;
    float tmp_min;
	float tmp_close;
};

bool WasResistanceBroken(const top& t, SCStudyInterfaceRef sc)
{
	SCDateTime topDate = sc.BaseDateTimeIn[t.i].GetDate();

	for (int i = t.i + 1; i <= sc.Index; i++)
	{
		// Zeit extrahieren
		SCDateTime dt = sc.BaseDateTimeIn[i];

		if (dt.GetDate() != topDate)
			continue;

		int hour = dt.GetHour();
		int minute = dt.GetMinute();

		bool inSession =
			(hour > 15 || (hour == 15 && minute >= 30)) &&
			(hour < 20);

		if (!inSession)
			continue;

		// Wurde das Top gebrochen?
		if (sc.Close[i] > t.tmp_max)
			return true;
	}	
	return false;
}

bool WasSupportBroken(const bottom& b, SCStudyInterfaceRef sc)
{
	SCDateTime bottomDate = sc.BaseDateTimeIn[b.i].GetDate();

	for (int i = b.i + 1; i <= sc.Index; i++)
	{
		// Zeit extrahieren
		SCDateTime dt = sc.BaseDateTimeIn[i];

		if (dt.GetDate() != bottomDate)
			continue;

		int hour = dt.GetHour();
		int minute = dt.GetMinute();

		bool inSession =
			(hour > 15 || (hour == 15 && minute >= 30)) &&
			(hour < 20);

		if (!inSession)
			continue;

		// Wurde das bottom gebrochen?
		if (sc.Close[i] < b.tmp_min)
			return true;
	}
	return false;
}

SCSFExport scsf_DirectionalChange3(SCStudyInterfaceRef sc)
{
    SCInputRef b_PrintLog = sc.Input[0];

    if (sc.SetDefaults)
    {
        b_PrintLog.Name = "Print newest Price";
        b_PrintLog.SetYesNo(false);

        sc.GraphRegion = 0;

        sc.GetPersistentPointer(0) = nullptr; // tops
        sc.GetPersistentPointer(1) = nullptr; // bottoms
		sc.UpdateAlways = 1;
		sc.SupportAttachedOrdersForTrading = true;
		sc.AllowMultipleEntriesInSameDirection = false;
		sc.MaximumPositionAllowed = 1;

        return;
    }
	

    // Persistent Werte
    int& up_zig = sc.GetPersistentInt(0);
    float& tmp_max = sc.GetPersistentFloat(10);
    float& tmp_min = sc.GetPersistentFloat(20);
    int& tmp_max_i = sc.GetPersistentInt(1);
    int& tmp_min_i = sc.GetPersistentInt(2);
	float& tmp_close = sc.GetPersistentFloat(30);
    float& sigma = sc.GetPersistentFloat(40);

    std::vector<top>* tops = (std::vector<top>*)sc.GetPersistentPointer(0);
    std::vector<bottom>* bottoms = (std::vector<bottom>*)sc.GetPersistentPointer(1);

    if (tops == nullptr)
    {
        tops = new std::vector<top>();
        sc.GetPersistentPointer(0) = tops;
    }

    if (bottoms == nullptr)
    {
        bottoms = new std::vector<bottom>();
        sc.GetPersistentPointer(1) = bottoms;
    }

    // Initialisierung nur auf Bar 1 (nicht bei jedem Full Recalc)
    if (sc.Index == 1)
    {
        tops->clear();
        bottoms->clear();

        tmp_max = 0.0f;
        tmp_min = 0.0f;
        tmp_max_i = 0;
        tmp_min_i = 0;
		tmp_close = 0.0f;
    }
	sigma = 0.003;

    // Reset der persistent Werte, wenn Replay geöffnet/geschlossen wird
		
	// Persistent Merker für letzten Replay-Zustand
	int& prevReplayState = sc.GetPersistentInt(500);

	// Aktueller Zustand
	bool isReplayNow = sc.IsReplayRunning();

	// === Übergang: Live → Replay ===
	if (!prevReplayState && isReplayNow)
	{
		sc.AddMessageToLog("Replay gestartet: Resette Persistent-Daten.", 0);

		tmp_max = 0.0f;
		tmp_min = 0.0f;
		tmp_max_i = 0;
		tmp_min_i = 0;
		tmp_close = 0.0f;

		if (tops != nullptr)
		tops->clear();
		
		if (bottoms != nullptr)
		bottoms->clear();
	}

	// === Übergang: Replay → Live ===
	if (prevReplayState && !isReplayNow)
	{
		sc.AddMessageToLog("Replay beendet: Resette Persistent-Daten.", 0);

		tmp_max = 0.0f;
		tmp_min = 0.0f;
		tmp_max_i = 0;
		tmp_min_i = 0;
		tmp_close = 0.0f;

		if (tops != nullptr)
			tops->clear();
		
		if (bottoms != nullptr)
			bottoms->clear();	
	}
	// Zustand speichern
	prevReplayState = isReplayNow;

	// Nur arbeiten, wenn die Bar abgeschlossen ist
	if (sc.GetBarHasClosedStatus() != BHCS_BAR_HAS_CLOSED)
		return;

	int c = sc.Index; // abgeschlossene Bar
	if (c <= 0)
		return;

	float high = sc.High[c];
	float low = sc.Low[c];
	float close = sc.Close[c];
	float open = sc.Open[c];
	
	// Zeitfilter 15.30 Uhr New York Session - 20 Uhr (variabel)

	SCDateTime dt = sc.BaseDateTimeIn[c];

	int hour = dt.GetHour();
	int minute = dt.GetMinute();
	bool allowTrading = false;

	if (
		(hour > 15 && hour < 20) ||
		(hour == 15 && minute >= 30) ||
		(hour == 20 && minute == 0)  
		)
	{
		allowTrading = true;
	}
	
	// der Befehl gehört zu dem Zeitfilter
	if (!allowTrading)
	{
		return; // Logik überspringen
	}
	
	// up_zig Berechnung mit SMA
	float SMA_G; // größerer SMA
	float SMA_K; // kleinerer SMA
	float SMA1;
	float SMA2;

	if (hour == 15 && minute == 30)
	{
		SMA1 = 0.0f;
		SMA2 = 0.0f;
		SMA_G = 0.0f;
		SMA_K = 0.0f;
		int N1 = 1;
		int N2 = 3;

		if (c >= N2)
		{
			for (int i = 0; i < N2; i++)
			{
				SMA_G += sc.Close[c - i];
				SMA_K += sc.Close[c - i];


				if (i == N1-1)
				{
					SMA1 = SMA_K / N1;
				}

				if (i == N2 - 1)
				{
					SMA2 = SMA_G / N2;
				}
			}
			up_zig = (SMA1 >= SMA2) ? 1 : 0;
				msg.Format("up_zig = %d", up_zig);
				sc.AddMessageToLog(msg, 0);
		}
	}

    // DIRECTIONAL CHANGE nur mit abgeschlossenen Bars
    if (up_zig == 1)
    {
        if (high > tmp_max)
        {
            tmp_max = high;
            tmp_max_i = c;
			tmp_close = max(close, open);

        }
        else if (close < tmp_max * (1.0f - sigma))
        {
            tops->push_back({ c, tmp_max_i, tmp_max, tmp_close });

            up_zig = 0;
            tmp_min = low;
            tmp_min_i = c;
			tmp_close  = min(close, open);
        }
    }
    else
    {
		if (hour == 15 && minute == 30)
		{
			tmp_min = FLT_MAX; //Erklärung: tmp_min um 15.30 Uhr auf FLT_MAX setzen, da sonst tmp_min = 0 wäre und dadurch direkt der else if code ausgelöst werden würde. Dadurch würden unwahre bottoms entstehen.
		}

        if (low < tmp_min)
        {
            tmp_min = low;
            tmp_min_i = c;
			tmp_close = min(close, open);
        }
        else if (close > tmp_min * (1.0f + sigma))
        {
            bottoms->push_back({ c, tmp_min_i, tmp_min, tmp_close });

            up_zig = 1;
            tmp_max = high;
            tmp_max_i = c;
			tmp_close = max(close, open);
        }
    }

    // Zeichnen nur des letzten Punktes
    s_UseTool Draw;
    Draw.Clear();
    Draw.DrawingType = DRAWING_MARKER;
    Draw.MarkerType = MARKER_POINT;
    Draw.AddMethod = UTAM_ADD_ALWAYS;

    if (!tops->empty())
    {
        const top& t = tops->back();
        Draw.Color = COLOR_GREEN;
        Draw.BeginIndex = t.tmp_max_i;
        Draw.BeginValue = t.tmp_max;
        Draw.LineNumber = 100000;
        sc.UseTool(Draw);
    }

    if (!bottoms->empty())
    {
        const bottom& b = bottoms->back();
        Draw.Color = COLOR_RED;
        Draw.BeginIndex = b.tmp_min_i;
        Draw.BeginValue = b.tmp_min;
        Draw.LineNumber = 200000;
        sc.UseTool(Draw);
    }
	
	if (b_PrintLog.GetYesNo())
    {
        SCString msg;
        msg.Format("Close[%d]=%f  tmp_max=%f  tmp_min=%f", c, close, tmp_max, tmp_min);
        sc.AddMessageToLog(msg, 0);
    }
	
	// Orderblock Algorithmus
	
	// potentiellen Widerstand/Support herauskriegen
	
	std::vector<const top*> validTops;

	for (const auto& t : *tops)
	{
		if (!WasResistanceBroken(t, sc))
			validTops.push_back(&t);
	}

	std::vector<const bottom*> validBottoms;

	for (const auto& b : *bottoms)
	{
		if (!WasSupportBroken(b, sc))
			validBottoms.push_back(&b);
	}
	
	
	// klelinsten gültigen potenzielen Widerstand über aktuellem Preis finden
	
	float potentialresistance = 0.0f;
	float PotRes_close = 0.0f;
	
	if (!validTops.empty())
	{
	
		float price = sc.Close[c];
	
		auto it_res = std::min_element(
			validTops.begin(),
			validTops.end(),
			[&](const top* a, const top* b) {
				// Nahegelegener Top über dem aktuellen Preis
				float da = (a->tmp_max > price) ? a->tmp_max : FLT_MAX;
				float db = (b->tmp_max > price) ? b->tmp_max : FLT_MAX;
				return da < db;
			}
		);
	
		if (it_res != validTops.end()) // validTops.end != der letzte Wert in validTops, sondern stellt sicher,
		{ 								// dass der Iterator nicht auf das Ende zeigt, also tatsächlich ein gültiges Element gefunden wurde
			potentialresistance = (*it_res)->tmp_max;
			PotRes_close = (*it_res)->tmp_close;
		}
	}
	
	float potentialsupport = 0.0f;
	float PotSup_close = 0.0f;

	if (!validBottoms.empty())
	{

		float price = sc.Close[c];

		auto it_sup = std::max_element(
			validBottoms.begin(),
			validBottoms.end(),
			[&](const bottom* a, const bottom* b) {
				// Nahegelegener bottom über dem aktuellen Preis
				float da = (a->tmp_min < price) ? a->tmp_min : -FLT_MAX; // Wert wird sehr klein gesetzt, wenn über Preis
				float db = (b->tmp_min < price) ? b->tmp_min : -FLT_MAX;
				return da < db;
			}
		);

		if (it_sup != validBottoms.end())
		{ 								
			potentialsupport = (*it_sup)->tmp_min;
			PotSup_close = (*it_sup)->tmp_close;
		}
	}

	//Überprüfung der Widerstände/Supports
    msg.Format("up_zig = %d SMA1 = %f SMA2 = %f der potentielle Widerstand ist = %f bis %f der potentielle Support ist = %f bis %f",up_zig, SMA1, SMA2, potentialresistance, PotRes_close, potentialsupport, PotSup_close);
    sc.AddMessageToLog(msg, 0);


//----------Orders setzen------------//

	if (sc.GetBarHasClosedStatus() != BHCS_BAR_HAS_CLOSED)
		return;

	s_SCPositionData pos;
	sc.GetTradePosition(pos);

	//Long entry
	if (close > PotSup_close && low < PotSup_close && pos.PositionQuantity == 0)
	{
		s_SCNewOrder order;

		order.OrderQuantity = 1;
		order.OrderType = SCT_ORDERTYPE_MARKET;

		order.AttachedOrderStop1Type = SCT_ORDERTYPE_STOP;
		order.Stop1Offset = (close - potentialsupport);

		order.AttachedOrderTarget1Type = SCT_ORDERTYPE_LIMIT;
		order.Target1Offset = 200;

		sc.BuyEntry(order);
	}
}


