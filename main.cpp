#include <iostream>
#include <cassert>
#include <functional>
#include <limits>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

const uint32_t MAX_CUR_NUMBER = 2000;
using CurId = uint64_t; // 1..2000
using RateFn = std::function<double()>; // Return 0 if rate is not available;

struct ConvertRate
{
  CurId  from;
  CurId  to;
  RateFn rateFn;
};

class IConverter
{
public:
  // precomputes all optimal pathes to calculate conversion rate between
  // any currency pair, if such conversion possible
  virtual void init(const std::vector<ConvertRate>& _rates) =0;
  virtual double convert(double value, CurId from, CurId to) =0;
  virtual ~IConverter() {}
};

// This implementation is faster on sparse graphs
class Converter : public IConverter
{
public:
  Converter()
  {
  }

  // Takes O(R * N^2) time and O(N^2) memory, where N - number of currencies
  // and R - number of currency rates
  // In worst case R = N^2, so overall complexity O(N^4)
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

class BFSConverter : public IConverter
{
public:
  BFSConverter()
  {
  }

  void init(const std::vector<ConvertRate>& rates)
  {
    mRates.clear();
    mPaths.clear();
    // add to mPaths all direct convert rates
    // and init mRates
    // complexity is O(R), worst case O(N^2)
    mRates.reserve(rates.size() + 1);
    mRates.push_back([]() { return 0.0; }); // add dummy fn
    mPaths.resize(MAX_CUR_NUMBER);
    for (const auto& rate : rates)
    {
      int32_t newRateId = static_cast<int32_t>(mRates.size());
      mRates.push_back(rate.rateFn);

      const CurId from = rate.from;
      const CurId to = rate.to;
      mPaths[from][to] = Cell(to, newRateId);
      mPaths[to][from] = Cell(from, -newRateId);
    }

    // Do BFS from each node to find all shortest paths
    // thus complexity is O(N(N + R)) = O(N^3)
    std::vector<CurId> visitedNodes(MAX_CUR_NUMBER, MAX_CUR_NUMBER);
    for (CurId from = 0; from < mPaths.size(); ++from)
    {
      visitedNodes[from] = from;
      // first - next to visit, second is started from
      using NextCur = std::pair<CurId, CurId>;
      std::list<NextCur> nextToVisitCurs;
      for (const auto& cell : mPaths[from])
      {
        const CurId next = cell.first;
        nextToVisitCurs.push_back(NextCur(next, next));
        visitedNodes[next] = from;
      }
      auto nextIt = nextToVisitCurs.begin();
      while (nextIt != nextToVisitCurs.end())
      {
        const CurId visitingId = nextIt->first;
        visitedNodes[visitingId] = from;
        for(const auto& nextPath : mPaths[visitingId])
        {
          auto& cell = nextPath.second;
          if (visitedNodes[cell.nextCur] != from)
          {
            visitedNodes[cell.nextCur] = from;
            nextToVisitCurs.emplace_back(cell.nextCur, nextIt->second);
            mPaths[from][cell.nextCur] = Cell(nextIt->second, Cell::norate);
          }
        }
        ++nextIt;
        nextToVisitCurs.pop_front();
      }
    }
  }

  // exchanges 'value' amount of currency 'from' to currency 'to' in O(N) time,
  // where N is minimal possible number of intermediate conversions
  double convert(double value, CurId from, CurId to)
  {
    if (mPaths[from].count(to) == 0)
      return 0.0d;
    if (from == to)
      return value;
    double totalRate = 1.0d;
    CurId prevCur = from;
    CurId nextCur = from;
    do
    {
      nextCur = mPaths[prevCur][to].nextCur;
      int32_t rateId = mPaths[prevCur][nextCur].rateId;
      if (rateId > 0)
      {
        totalRate *= mRates[rateId]();
      }
      else
      {
        double rate = mRates[-rateId]();
        if (rate == 0)
          totalRate = 0;
        else
          totalRate /= mRates[-rateId]();
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
    Cell(CurId _nextCur, int32_t _rateId)
      : rateId(_rateId)
      , nextCur(_nextCur)
    {}
  };
  std::vector<std::unordered_map<CurId, Cell>> mPaths;
  std::vector<RateFn> mRates;
};

// It's not canonical GOF Factory
class ConverterFactory
{
public:
  enum class Type { INCREMENTAL, BFS};
  void setType(Type type) { mType = type; }
  std::unique_ptr<IConverter> create() const
  {
    switch(mType)
    {
      case Type::INCREMENTAL:
        return std::make_unique<Converter>();
      case Type::BFS:
        return std::make_unique<BFSConverter>();
    }
    return std::make_unique<BFSConverter>();
  }
private:
  Type mType;
};

void runTests(const ConverterFactory& factory)
{
  using namespace std;
  {
    cout << "Test 1 one rate smoke";
    vector<ConvertRate> rates{{0,1,[](){return 2.0;}}};
    auto cvt = factory.create();
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
    auto cvt = factory.create();
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
    auto cvt = factory.create();
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
    auto cvt = factory.create();
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
    auto cvt = factory.create();
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
    auto cvt = factory.create();
    cvt->init(rates);
    assert(cvt->convert(100.0, 0, 1)  == 200.0);
    assert(cvt->convert(100.0, MAX_CUR_NUMBER - 2, 0)  == 400.0);
    cout << " end" << endl;
  }
}

int main()
{
  ConverterFactory factory;
  factory.setType(ConverterFactory::Type::BFS);
  runTests(factory);
  return 0;
}
