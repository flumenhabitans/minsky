/*
  @copyright Steve Keen 2018
  @author Russell Standish
  This file is part of Minsky.

  Minsky is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Minsky is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Minsky.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "CSVParser.h"
#include <ecolab_epilogue.h>
using namespace minsky;
using namespace std;

#include <boost/type_traits.hpp>
#include <boost/tokenizer.hpp>
#include <boost/token_functions.hpp>

typedef boost::escaped_list_separator<char> Parser;
typedef boost::tokenizer<Parser> Tokenizer;

struct NoDataColumns: public exception
{
  const char* what() const noexcept override {return "No data columns";}
};
struct DuplicateKey: public exception
{
  const char* what() const noexcept override {return "Duplicate key";}
};

namespace
{
  const size_t maxRowsToAnalyse=100;
  
  // returns first position of v such that all elements in that or later
  // positions are numerical or null
  size_t firstNumerical(const vector<string>& v)
  {
    size_t r=0;
    for (size_t i=0; i<v.size(); ++i)
      try
        {
          if (!v[i].empty())
            stod(v[i]);
        }
      catch (...)
        {
          r=i+1;
        }
    return r;
  }

  // counts number of non empty entries on a line
  size_t numEntries(const vector<string>& v)
  {
    size_t c=0;
    for (auto& x: v)
      if (!x.empty())
        c++;
    return c;
  }
  
  // returns true if all elements of v after start are empty
  bool emptyTail(const vector<string>& v, size_t start)
  {
    for (size_t i=start; i<v.size(); ++i)
      if (!v[i].empty()) return false;
    return true;
  }
}

void DataSpec::setDataArea(size_t row, size_t col)
{
  m_nRowAxes=row;
  m_nColAxes=col;
  if (headerRow>=row)
    headerRow=row>0? row-1: 0;
  if (dimensions.size()<nColAxes()) dimensions.resize(nColAxes());
  if (dimensionNames.size()<nColAxes()) dimensionNames.resize(nColAxes());
  // remove any dimensionCols > nColAxes
  dimensionCols.erase(dimensionCols.lower_bound(nColAxes()), dimensionCols.end());
}


template <class TokenizerFunction>
void DataSpec::givenTFguessRemainder(std::istream& input, const TokenizerFunction& tf)
{
    vector<size_t> starts;
    size_t nCols=0;
    string buf;
    size_t row=0;
    size_t firstEmpty=numeric_limits<size_t>::max();
    dimensionCols.clear();
    
    for (; getline(input, buf) && row<maxRowsToAnalyse; ++row)
      {
        boost::tokenizer<TokenizerFunction> tok(buf.begin(),buf.end(), tf);
        vector<string> line(tok.begin(), tok.end());
        starts.push_back(firstNumerical(line));
        nCols=max(nCols, line.size());
        if (starts.size()-1 < firstEmpty && starts.back()<nCols && emptyTail(line, starts.back()))
          firstEmpty=starts.size()-1;
      }
    // compute average of starts, then look for first row that drops below average
    double sum=0;
    for (unsigned long i=0; i<starts.size(); ++i) 
      sum+=starts[i];
    double av=sum/(starts.size());
    for (m_nRowAxes=0; starts.size()>m_nRowAxes && (starts[m_nRowAxes]>av || starts[m_nRowAxes]==1); 
         ++m_nRowAxes);
    m_nColAxes=0;
    for (size_t i=nRowAxes(); i<starts.size(); ++i)
      m_nColAxes=max(m_nColAxes,starts[i]);
    // if more than 1 data column, treat the first row as an axis row
    if (m_nRowAxes==0 && nCols-m_nColAxes>1)
      m_nRowAxes=1;
    
    if (firstEmpty==m_nRowAxes) ++m_nRowAxes; // allow for possible colAxes header line
    headerRow=nRowAxes()>0? nRowAxes()-1: 0;
    for (size_t i=0; i<nColAxes(); ++i) dimensionCols.insert(i);
}

void DataSpec::guessRemainder(std::istream& input, char sep)
{
  separator=sep;
  if (separator==' ')
    givenTFguessRemainder(input,boost::char_separator<char>()); //asumes merged whitespace separators
  else
    givenTFguessRemainder(input,Parser(escape,separator,quote));
}


void DataSpec::guessFromStream(std::istream& input)
{
  size_t numCommas=0, numSemicolons=0, numTabs=0;
  size_t row=0;
  string buf;
  ostringstream streamBuf;
  for (; getline(input, buf) && row<maxRowsToAnalyse; ++row, streamBuf<<buf<<endl)
    for (auto c:buf)
      switch (c)
        {
        case ',':
          numCommas++;
          break;
        case ';':
          numSemicolons++;
          break;
        case '\t':
          numTabs++;
          break;
        }

  {
    istringstream inputCopy(streamBuf.str());
    if (numCommas>0.9*row && numCommas>numSemicolons && numCommas>numTabs)
      guessRemainder(inputCopy,',');
    else if (numSemicolons>0.9*row && numSemicolons>numTabs)
      guessRemainder(inputCopy,';');
    else if (numTabs>0.9*row)
      guessRemainder(inputCopy,'\t');
    else
      guessRemainder(inputCopy,' ');
  }

  {
    //fill in guessed dimension names
    istringstream inputCopy(streamBuf.str());
    guessDimensionsFromStream(inputCopy);
  }
}

void DataSpec::guessDimensionsFromStream(std::istream& i)
{
  if (separator==' ')
    guessDimensionsFromStream(i,boost::char_separator<char>());
  else
    guessDimensionsFromStream(i,Parser(escape,separator,quote));
}
    
template <class T>
void DataSpec::guessDimensionsFromStream(std::istream& input, const T& tf)
{
  string buf;
  size_t row=0;
  for (; row<=headerRow; ++row) getline(input, buf);
  boost::tokenizer<T> tok(buf.begin(),buf.end(), tf);
  dimensionNames.assign(tok.begin(), tok.end());
  for (;row<=nRowAxes(); ++row) getline(input, buf);
  vector<string> data(tok.begin(),tok.end());
  for (size_t col=0; col<data.size() && col<nColAxes(); ++col)
    try
      {
        Dimension dim(Dimension::value,"");
        anyVal(dim, data[col]);
        dimensions.push_back(dim);
      }
    catch (...)
      {
        try
          {
            Dimension dim(Dimension::time,"");
            anyVal(dim, data[col]);
            dimensions.push_back(dim);
          }
        catch (...)
          {
            try
              {
                Dimension dim(Dimension::time,"%Y-Q%Q");
                anyVal(dim, data[col]);
                dimensions.push_back(dim);
              }
            catch (...)
              {
                dimensions.emplace_back(Dimension::string,"");
              }
          }
      }
}




namespace minsky
{

  void loadValueFromCSVFile(VariableValue& v, istream& input, const DataSpec& spec)
  {
    Parser csvParser(spec.escape,spec.separator,spec.quote);
    string buf;
    typedef vector<string> Key;
    map<Key,double> tmpData;
    vector<map<string,size_t>> dimLabels(spec.dimensionCols.size());
    bool tabularFormat=false;
    vector<XVector> xVector;
    vector<string> horizontalLabels;

    for (size_t i=0; i<spec.nColAxes(); ++i)
      if (spec.dimensionCols.count(i))
        xVector.emplace_back(i<spec.dimensionNames.size()? spec.dimensionNames[i]: "dim"+str(i));

    for (size_t row=0; getline(input, buf); ++row)
      {
        Tokenizer tok(buf.begin(), buf.end(), csvParser);

        assert(spec.headerRow<=spec.nRowAxes());
        if (row==spec.headerRow && !spec.columnar) // in header section
          {
            vector<string> parsedRow(tok.begin(), tok.end());
            if (parsedRow.size()>spec.nColAxes()+1)
              {
                tabularFormat=true;
                horizontalLabels.assign(parsedRow.begin()+spec.nColAxes(), parsedRow.end());
                xVector.emplace_back(spec.horizontalDimName);
                for (auto& i: horizontalLabels) xVector.back().push_back(i);
                dimLabels.emplace_back();
                for (size_t i=0; i<horizontalLabels.size(); ++i)
                  dimLabels.back()[horizontalLabels[i]]=i;
              }
          }
        else if (row>=spec.nRowAxes())// in data section
          {
            Key key;
            auto field=tok.begin();
            for (size_t i=0, dim=0; i<spec.nColAxes() && field!=tok.end(); ++i, ++field)
              if (spec.dimensionCols.count(i))
                {
                  if (dim>=xVector.size())
                    xVector.emplace_back("?"); // no header present
                  key.push_back(*field);
                  if (dimLabels[dim].emplace(*field, dimLabels[dim].size()).second)
                    xVector[dim].push_back(*field);
                  dim++;
                }
                    
            if (field==tok.end())
              throw NoDataColumns();
          
            for (size_t col=0; field != tok.end(); ++field, ++col)
              {
                if (tabularFormat)
                  key.push_back(horizontalLabels[col]);
                if (tmpData.count(key))
                  throw DuplicateKey();
                try
                  {
                    tmpData[key]=stod(*field);
                  }
                catch (...)
                  {
                    tmpData[key]=spec.missingValue;
                  }
                if (tabularFormat)
                  key.pop_back();
              }
          }
      }
  
    v.setXVector(xVector);
    // stash the data into vv tensorInit field
    v.tensorInit.data.clear();
    v.tensorInit.data.resize(v.numElements(), spec.missingValue);
    auto dims=v.tensorInit.dims=v.dims();    
    for (auto& i: tmpData)
      {
        size_t idx=0;
        assert (dims.size()==i.first.size());
        assert(dimLabels.size()==dims.size());
        for (int j=dims.size()-1; j>=0; --j)
          {
            assert(dimLabels[j].count(i.first[j]));
            idx = (idx*dims[j]) + dimLabels[j][i.first[j]];
          }
        v.tensorInit.data[idx]=i.second;
      }
  }

}
