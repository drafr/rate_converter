#include <iostream>
#include <functional>
#include <vector>
#include <limits>
#include <cassert>
#include <memory>

const uint32_t MAX_CUR_NUMBER = 2000;
using CurId = uint64_t; // 1..2000
using RateFn = std::function<double()>; // Return 0 if rate is not available;

struct ConvertRate
{
  CurId  from;
  CurId  to;
  RateFn rateFn;
};

class Converter
{
public:
  Converter()
  {
  }

  // precomputes all optimal pathes to calculate conversion rate between
  // any currency pair, if such conversion possible
  // Takes O(R * N^2) time and O(N^2) memory, where N - number of currencies
  // and R - number of currency rates
  void init(const std::vector<ConvertRate>& _rates)
  {
    Cell defaultCell;
    for (size_t i = 0; i < MAX_CUR_NUMBER; ++i)
      for (size_t j = 0; j < MAX_CUR_NUMBER; ++j)
        rate_table[i][j] = defaultCell;

    rates.reserve(_rates.size() + 1);
    rates.push_back([]() { return 0.0; }); // add dummy fn

    using Distance = uint32_t;
    // minimal conversion distance between two currencies
    std::vector<std::vector<Distance>> distance(MAX_CUR_NUMBER);
    // max distance meaning that two currencies can't be converted
    const Distance unreachable = std::numeric_limits<Distance>::max();
    for (size_t i = 0; i < MAX_CUR_NUMBER; ++i)
    {
      distance[i].assign(MAX_CUR_NUMBER, unreachable);
      distance[i][i] = 0;
    }
    for (const auto& rate : _rates)
    {
      const CurId from = rate.from;
      const CurId to = rate.to;
      int32_t newRateId = static_cast<int32_t>(rates.size());
      rates.push_back(rate.rateFn);
      rate_table[from][to].rateId = newRateId;
      rate_table[to][from].rateId = -newRateId;

      for (size_t i = 0; i < MAX_CUR_NUMBER; ++i)
      {
        for (size_t j = 0; j < MAX_CUR_NUMBER; ++j)
        {
          if (distance[from][i] != unreachable && distance[to][j] != unreachable)
          {
            Distance new_distance = distance[from][i] + distance[to][j] + 1;
            if (new_distance >= distance[i][j])
              continue;
            distance[i][j] = distance[j][i] = new_distance;
            rate_table[i][j].nextCur = i != from ? rate_table[i][from].nextCur : to;
            rate_table[j][i].nextCur = j != to ? rate_table[j][to].nextCur : from;
          }
        }
      }
    }
  }

  // exchanges 'value' amount of currency 'from' to currency 'to' in O(N) time,
  // where N is minimal possible number of intermediate conversions
  double convert(double value, CurId from, CurId to)
  {
    if (rate_table[from][to].nextCur == MAX_CUR_NUMBER)
      return 0.0d;
    double totalRate = 1.0d;
    CurId prevCur = from;
    CurId nextCur = from;
    do
    {
      nextCur = rate_table[prevCur][to].nextCur;
      int32_t rateId = rate_table[prevCur][nextCur].rateId;
      if (rateId > 0)
      {
        totalRate *= rates[rateId]();
      }
      else
      {
        double rate = rates[-rateId]();
        if (rate == 0)
          totalRate = 0;
        else
          totalRate /= rates[-rateId]();
      }
      prevCur = nextCur;
    } while (nextCur != to);
	
	// weird compiler behavior, when multiplication result is 0 (seems casts to int)
	double finalValue = static_cast<long double>(totalRate) * static_cast<long double>(value);
    return finalValue;
  }

private:
  struct Cell
  {
    int32_t rateId;
    static const int32_t norate{0};
    CurId nextCur;
    Cell()
      : rateId(norate)
      , nextCur(MAX_CUR_NUMBER)
    {}
  };
  Cell rate_table[2000][2000];
  std::vector<RateFn> rates;
};

int main()
{
    using namespace std;
    // tests
    {
      cout << "Test 1 one rate smoke";
      vector<ConvertRate> rates{{0,1,[](){return 2.0;}}};
      unique_ptr<Converter> cvt{make_unique<Converter>()};
      cvt->init(rates);
      assert(cvt->convert(100.0, 0, 1) == 200.0);
      assert(cvt->convert(100.0, 1, 0) == 50.0);
      cout << " end" << endl;
    }
    {
      cout << "Test 2 two independent rates";
      vector<ConvertRate> rates{
        {0,1,[](){return 2.0;}},
        {2,3,[](){return 4.0;}}
      };
      unique_ptr<Converter> cvt{make_unique<Converter>()};
      cvt->init(rates);
      assert(cvt->convert(100.0, 0, 1) == 200.0);
      assert(cvt->convert(100.0, 1, 0) == 50.0);
      assert(cvt->convert(100.0, 2, 3) == 400.0);
      assert(cvt->convert(400.0, 3, 2) == 100.0);
      cout << " end" << endl;
    }
    {
      cout << "Test 3 sequential rates";
      vector<ConvertRate> rates{
        {0,1,[](){return 2.0;}},
        {1,2,[](){return 3.0;}},
        {2,3,[](){return 4.0;}}
      };
      unique_ptr<Converter> cvt{make_unique<Converter>()};
      cvt->init(rates);
      assert(cvt->convert(100.0, 0, 1) == 200.0);
      assert(cvt->convert(100.0, 1, 0) == 50.0);
      assert(cvt->convert(100.0, 2, 3) == 400.0);
      assert(cvt->convert(400.0, 3, 2) == 100.0);
      assert(cvt->convert( 10.0, 0, 3) == 240.0);
      assert(cvt->convert(240.0, 3, 0) == 10.0);
      cout << " end" << endl;
    }
    {
      cout << "Test 4 merge two rate graphs";
      vector<ConvertRate> rates{
        {0,1,[](){return 2.0;}},
        {2,3,[](){return 4.0;}},
        {1,2,[](){return 3.0;}}
      };
      unique_ptr<Converter> cvt{make_unique<Converter>()};
      cvt->init(rates);
      assert(cvt->convert(100.0, 0, 1) == 200.0);
      assert(cvt->convert(100.0, 1, 0) == 50.0);
      assert(cvt->convert(100.0, 2, 3) == 400.0);
      assert(cvt->convert(400.0, 3, 2) == 100.0);
      assert(cvt->convert( 10.0, 0, 3) == 240.0);
      assert(cvt->convert(240.0, 3, 0) == 10.0);
      cout << " end" << endl;
    }
    {
      cout << "Test 5 choose shortest path";
      vector<ConvertRate> rates{
        {0,1,[](){return 2.0;}},
        {1,2,[](){return 3.0;}},
        {2,3,[](){return 4.0;}},
        {4,3,[](){return 5.0;}},
        {0,4,[](){return 6.0;}},
      };
      unique_ptr<Converter> cvt{make_unique<Converter>()};
      cvt->init(rates);
      assert(cvt->convert(100.0, 0, 3)  == 3000.0);
      assert(cvt->convert(3000.0, 3, 0) == 100.0);
      cout << " end" << endl;
    }
    {
      cout << "Test 6 max N smoke";
      vector<ConvertRate> rates;
      for(CurId i = 0; i < MAX_CUR_NUMBER - 1; ++i)
      {
        rates.push_back({i, i+1, [](){return 2.0;}});
      };
      rates.push_back({ MAX_CUR_NUMBER - 1, 0, [](){return 2.0;}});
      unique_ptr<Converter> cvt{make_unique<Converter>()};
      cvt->init(rates);
      assert(cvt->convert(100.0, 0, 1)  == 200.0);
      assert(cvt->convert(100.0, MAX_CUR_NUMBER - 2, 0)  == 400.0);
      cout << " end" << endl;
    }
    return 0;
}
