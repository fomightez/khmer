#ifndef BLEUFILTER_HPP
#define BLEUFILTER_HPP

#include "../../lib/hashtable.hh"
#include "../../lib/parsers.hh"
#include "../external_lib/cBitArray.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <list>
#include <map>
#include <sstream>
#include <set>
#include <assert.h>
#include <time.h>

#define CALLBACK_PERIOD 10000
#define BIT_COUNT_PARTITION 1000

#define HASHES 8

namespace bleu {
  
  using namespace khmer;
  using namespace std;
  
  typedef unsigned short SetID;
  typedef unsigned long long MaxSize;
  
  class BleuFilter
  : public Hashtable
  {
    class Set
    {
    private:
      vector<Set**> BackReferences;
      BleuFilter * Parent;
      
      
    public: 
      Set ** Self;
      
      unsigned int SetOffset;
      
      unsigned long long Count;
      
    public:
      Set( BleuFilter * aParent, unsigned int aSetOffset )      
      {        
        Self = new Set*();
        *Self = this;

        Parent = aParent;
        BackReferences.push_back( Self );
        
        SetOffset = aSetOffset;
        
        Count = 0;
      }
      
      ~Set()
      {
        for ( vector<Set**>::iterator lReference = BackReferences.begin(); lReference != BackReferences.end(); ++lReference )
        {
          assert ( **lReference != this );
        }  
      }
      
      unsigned long long size()
      {
        return BackReferences.size();
      }
      
      void consume( Set * aSet )
      {         
        for ( int i = 0; i < aSet->BackReferences.size(); ++i )
        {
          *(aSet->BackReferences[i]) = this;
          BackReferences.push_back( aSet->BackReferences[i] );
        }
        
        Count += aSet->Count;
        
        Parent->_unique_sets.erase( aSet );
        
        delete aSet; // this should work.
      }
     

    };
    
  public:
    typedef unsigned int (BleuFilter::*ConsumeStringFN)(const std::string &filename,
                                                       HashIntoType upper_bound,
                                                       HashIntoType lower_bound);

    
  protected:    
    // sizes set during constructor
    cBitArray * _hash_table_preliminary[HASHES]; // two-dimensional hash-table
    cBitArray * _hash_table[HASHES]; // two-dimensional hash-table

    unsigned long long _tablesizes[HASHES]; // the sizes of the hash tables
    unsigned long long * _hash_table_bit_counts_lookup[HASHES]; // the number of bits set in the hash table, by every 10k entries
    unsigned int _last_set_offset;

    // sizes set during prep 1 (based on processing of _hash_table)
    unsigned long long _hash_table_total_bit_counts[HASHES];
    //unsigned int * _set_offsets[HASHES]; // based on the totals
    unsigned short * _set_offsets[HASHES]; 

    // contents set during (based on the processing of the reads)  
    vector<Set**> _sets; // array of pointers sets (really? seriously?)
    
    set<Set*> _unique_sets;
    unsigned long long _smallest_set_count;
    
  public:
    BleuFilter(WordLength ksize, unsigned long long tablesize)
    : Hashtable(ksize, get_first_prime_below( tablesize / HASHES ))
    {       
      _last_set_offset = 0; // init to zero
      _smallest_set_count = 0;
      _sets.push_back(NULL); // the 0th entry is invalid.
      
      _tablesizes[0] = _tablesize;
      for ( int i = 1; i < HASHES; ++i )      
      {
        _tablesizes[i] = get_first_prime_below(_tablesizes[i-1]); 
      }
     
      cout << "XXX" << sizeof(unsigned short) << endl;
 
      for ( int j = 0; j < HASHES; ++j )      
      {
        _hash_table_preliminary[j] = new cBitArray( _tablesizes[j] );
        _hash_table_preliminary[j]->Clear();      
        
        _hash_table[j] = new cBitArray( _tablesizes[j] );
        _hash_table[j]->Clear(); 
                    
        _hash_table_bit_counts_lookup[j] = new unsigned long long[(_tablesizes[j] / BIT_COUNT_PARTITION)+1];
        memset(_hash_table_bit_counts_lookup[j], 0, ((_tablesizes[j] / BIT_COUNT_PARTITION)+1) * sizeof(unsigned long long));
                  
        // THESE WILL GET SET LATER.            
        _hash_table_total_bit_counts[j] = 0;
        _set_offsets[j] = NULL; // a table the size of the number of bits in the hash table.
      }

      // housekeeping
      _counts = NULL;   
    }
    
    void populate_hash_table_bit_count_lookups()
    {
      cout << "populate_hash_table_bit_count_lookups" << endl;
      for (int i = 0; i < HASHES; ++i )
      {
        _hash_table_total_bit_counts[i] = _hash_table[i]->CountBits();
        
        unsigned long long lLookupTableSize = (_tablesizes[i] / BIT_COUNT_PARTITION)+1;
        for (unsigned long long j = 0; j < lLookupTableSize; ++j)
        {      
          unsigned long long lSectionStartIndex = j * BIT_COUNT_PARTITION;
          unsigned long long lSectionStopIndex = ((j + 1) * BIT_COUNT_PARTITION) - 1;
          if ( lSectionStopIndex >= _tablesizes[i] )
            lSectionStopIndex = _tablesizes[i] - 1;
          
          // temporarily store the single section count. (this section may be problematic, since ..lookup[i]'s value is a pointer, so what does [j] do?
          _hash_table_bit_counts_lookup[i][j] = _hash_table[i]->CountBits(lSectionStartIndex, lSectionStopIndex);
          
          if ( j > 0 ) // apply the summation
          {
            _hash_table_bit_counts_lookup[i][j] += _hash_table_bit_counts_lookup[i][j-1];
          }
        }
        cout << i << ": " << _hash_table_total_bit_counts[i] << " -- " << ((double)_hash_table_total_bit_counts[i] / (double)_tablesizes[i]) * 100 << "% occupancy" << endl;
      }
    }
    
    void deallocate_hash_table_preliminary()
    {
      for (int i = 0; i < HASHES; ++i)
      {
        delete _hash_table_preliminary[i];
      }      
    }
    
    void allocate_set_offset_table()
    {
      cout << "sizeof short" << sizeof(unsigned short) << endl;
      for (int i = 0; i < HASHES; ++i)
      {
        _set_offsets[i] = new unsigned short[ _hash_table_total_bit_counts[i] ];
        memset(_set_offsets[i], 0, _hash_table_total_bit_counts[i] * sizeof(unsigned short));
      }
    }
        
    // consume_string: run through every k-mer in the given string, & hash it.
    // overriding the Hashtable version to support my new thang.
    unsigned int consume_string_for_hash_table(const std::string &s,
                                HashIntoType lower_bound = 0,
                                HashIntoType upper_bound = 0)
    {
      const char * sp = s.c_str();
      const unsigned int length = s.length();
      unsigned int n_consumed = 0;
      
      HashIntoType forward_hash = 0, reverse_hash = 0;
      
      // generate the hash for the first kmer in the read (fair amount of work)
      HashIntoType hash = _hash(sp, _ksize, forward_hash, reverse_hash);
      
      bool lHasASet = false;
      for (int i = 0; i < HASHES; ++i )
      {
        unsigned long long lHashBin = HashToHashBin(hash, i); 
        
        if ( _hash_table_preliminary[i]->Get(lHashBin) == true )
        {
          _hash_table[i]->Set(lHashBin, true);
        }
        else
        {
          _hash_table_preliminary[i]->Set(lHashBin, true);
        }
      }
      
      n_consumed++;
      
      // now, do the rest of the kmers in this read (add one nt at a time)
      for (unsigned int i = _ksize; i < length; i++) {
        hash = _move_hash_foward( forward_hash, reverse_hash, sp[i] );        
        
        for (int j = 0; j < HASHES; ++j )
        {
          unsigned long long lHashBin = HashToHashBin(hash, j);// next_hash % _tablesizes[i];
          if ( _hash_table_preliminary[j]->Get(lHashBin) == true )
          {
            _hash_table[j]->Set(lHashBin, true);
          }
          else
          {
            _hash_table_preliminary[j]->Set(lHashBin, true);
          }
        }
        
        n_consumed++;
      }
      
      return n_consumed;
    }
    
    
    // consume_string: run through every k-mer in the given string, & hash it.
    // overriding the Hashtable version to support my new thang.
    unsigned int consume_string_for_set(const std::string &s,
                                HashIntoType lower_bound = 0,
                                HashIntoType upper_bound = 0)
    {
      const char * sp = s.c_str();
      const unsigned int length = s.length();
      unsigned int n_consumed = 0;
      
      HashIntoType forward_hash = 0, reverse_hash = 0;
      
      // generate the hash for the first kmer in the read (fair amount of work)
      HashIntoType hash = _hash(sp, _ksize, forward_hash, reverse_hash);
      
      // for the first hash     
      Set * lWorkingSet = SelectOrCreateProperSet( hash );
      
      if ( lWorkingSet != NULL )
        lWorkingSet->Count++;
      
      ++n_consumed;
      
      // for the rest of the string
      // now, do the rest of the kmers in this read (add one nt at a time)      
      for (unsigned int i = _ksize; i < length; i++) 
      {
        hash = _move_hash_foward( forward_hash, reverse_hash, sp[i] ); 
        
        if (lWorkingSet == NULL)
          lWorkingSet = SelectOrCreateProperSet(hash);
        else
          lWorkingSet = AssignOrBridgeToProperSet( hash, lWorkingSet );
        
        if ( lWorkingSet != NULL )
          lWorkingSet->Count++;
        
        ++n_consumed;
      }      
      
      return n_consumed;
    }
    
    Set * SelectOrCreateProperSet( HashIntoType aHash )
    { 
      unsigned long long lProspectiveHashBins[HASHES];
      memset(lProspectiveHashBins, 0, HASHES * sizeof( unsigned long long ));
      
      bool lHasASet = false;
      for (int i = 0; i < HASHES; ++i)
      {
        unsigned long long lHashBin = HashToHashBin( aHash, i );
        
        if ( i == 0 ) // the first one gets the ball rolling
          lHasASet = _hash_table[i]->Get( lHashBin );
        else if ( _hash_table[i]->Get( lHashBin ) != lHasASet ) // we don't have agreement, so the others must have been false positives for has set.
        {
          //cout << ",";
          return NULL;
        }
          
        
        lProspectiveHashBins[i] = lHashBin;
      }
      if ( lHasASet == false )
        return NULL;

      // ok, we're still here, so we might have a set. Let's go get it.
      unsigned long long lProspectiveSetOffsetBins[HASHES];
      memset(lProspectiveSetOffsetBins, 0, HASHES * sizeof( unsigned long long ));
      Set * lProspectiveSets[HASHES];
      memset(lProspectiveSets, 0, HASHES * sizeof(Set*));
      for ( int i = 0; i < HASHES; ++i )
      {
        lProspectiveSetOffsetBins[i] = HashBinToSetOffsetBin( lProspectiveHashBins[i], i );
        lProspectiveSets[i] = SetOffsetToSet( SetOffsetBinToSetOffset( lProspectiveSetOffsetBins[i], i) );
      }
      
      Set * lSet = NULL;      
      for (int i = 0; i < HASHES; ++i)
      {
        if ( lProspectiveSets[i] == NULL ) // we found an empty slot! Woohoo!
        {
          if ( lSet == NULL ) // put a new set there. if there's no room for a set, well...
            lSet = init_new_set();        
          
          _set_offsets[i][ lProspectiveSetOffsetBins[i] ] = lSet->SetOffset; // set them all          
        }
      }      
      
      if ( lSet != NULL ) // we just created a new set, so we'll go with that.
      {
        _sets[ lSet->SetOffset ] = lSet->Self; // this may be unnecessary (if init gave us an existing set to join), but why not.
        return lSet;
      }
      
              
      // So, none of them were empty. Perform pairwise comparisons of the hashes to see which agree.
      map<Set *, int> lRepresented;
      for (int i = 0; i < HASHES; ++i)
      {
        lRepresented[ lProspectiveSets[i] ]++;
      }      
      Set * lMostRepresented = NULL; // because maps are sorted, this will be the set with the lowest pointer, and the highest count.
      for ( map<Set*, int>::iterator lSet = lRepresented.begin(); lSet != lRepresented.end(); ++lSet )
      {
        if ( lMostRepresented == NULL || lSet->second > lRepresented[ lMostRepresented ] )
          lMostRepresented = lSet->first;
      }     
      
      if ( lRepresented[lMostRepresented] < 8 )
        cout << lRepresented[lMostRepresented];


      return lMostRepresented;
    }
    
    Set * AssignOrBridgeToProperSet( HashIntoType aHash, Set * aWorkingSet )
    { 
      unsigned long long lProspectiveHashBins[HASHES];
      memset(lProspectiveHashBins, 0, HASHES * sizeof( unsigned long long ));
      
      bool lHasASet = false;
      for (int i = 0; i < HASHES; ++i)
      {
        unsigned long long lHashBin = HashToHashBin( aHash, i );
        
        if ( i == 0 ) // the first one gets the ball rolling
          lHasASet = _hash_table[i]->Get( lHashBin );
        else if ( _hash_table[i]->Get( lHashBin ) != lHasASet ) // we don't have agreement, so the others must have been false positives for having a set.
        {
          //cout << ".";        
          return NULL;
        }
        
        lProspectiveHashBins[i] = lHashBin;
      }
      if ( lHasASet == false )
        return NULL;
      
      // ok, we're still here, so we might have a set. Let's go do it.
      
      unsigned long long lProspectiveSetOffsetBins[HASHES];
      memset(lProspectiveSetOffsetBins, 0, HASHES * sizeof( unsigned long long ));
      
      Set * lProspectiveSets[HASHES];
      memset(lProspectiveSets, 0, HASHES * sizeof(Set*));
      for ( int i = 0; i < HASHES; ++i )
      {
        lProspectiveSetOffsetBins[i] = HashBinToSetOffsetBin( lProspectiveHashBins[i], i );
        lProspectiveSets[i] = SetOffsetToSet( SetOffsetBinToSetOffset( lProspectiveSetOffsetBins[i], i) );
      }
      
      Set * lSet = NULL;      
      for (int i = 0; i < HASHES; ++i)
      {
        if ( lProspectiveSets[i] == NULL ) // we found an empty slot! Woohoo!
        {
          if ( lSet == NULL ) // put a new set there. if there's no room for a set, well...
            lSet = aWorkingSet;      
          
          _set_offsets[i][ lProspectiveSetOffsetBins[i] ] = lSet->SetOffset; // set them all
        }
      }      
      
      if ( lSet != NULL )
      {
        _sets[ lSet->SetOffset ] = lSet->Self; // this may be unnecessary (if init gave us an existing set to join), but why not.
        return lSet;
      }     
      
      // so, none of them were empty. were any of them us?
      map<Set *, int> lRepresented;      
      lRepresented.insert( map<Set*, int>::value_type(aWorkingSet, 1) );      
      for (int i = 0; i < HASHES; ++i)
      {        
        lRepresented[ lProspectiveSets[i] ]++;
      }
      Set * lMostRepresented = NULL; // because maps are sorted, this will be the set with the lowest pointer, and the highest count.
      for ( map<Set*, int>::iterator lSet = lRepresented.begin(); lSet != lRepresented.end(); ++lSet )
      {
        if ( lMostRepresented == NULL || lSet->second > lRepresented[ lMostRepresented ] )
          lMostRepresented = lSet->first;
      }
     
      if ( lRepresented[lMostRepresented] < 9 )
        cout << lRepresented[lMostRepresented];
      
      if ( lMostRepresented != aWorkingSet )
        return bridge_sets( lMostRepresented, aWorkingSet);
      else
        return aWorkingSet;
    } 
    
    
//    void OutputAsBits( unsigned long long aValue )
//    {
//      unsigned long long aMask = 1ULL << 63;
//      for ( int i = 0; i < 64; ++i )
//      {
//        unsigned long long aMasked = aValue & aMask;
//        cout << (aMasked >> 63);
//        aValue = aValue << 1;
//      }
//    }
    
    unsigned long long HashToHashBin ( HashIntoType aHash, int i )
    {
      return (aHash % _tablesizes[i]);
    }

    unsigned long long HashBinToSetOffsetBin( unsigned long long aBin, int i )
    {      
      assert( aBin < _tablesizes[i] ); // make sure it's a valid bin.
      
      assert( _hash_table[i]->Get( aBin ) == true );
      
      unsigned long long lBinSectionIndex = (aBin / BIT_COUNT_PARTITION); // the index of the section before the one we're in
      assert( lBinSectionIndex <= (_tablesizes[i] / BIT_COUNT_PARTITION)); 
      
      unsigned long long lSetOffsetBin = _hash_table[i]->CountBits( lBinSectionIndex * BIT_COUNT_PARTITION, aBin );
            
      if ( lBinSectionIndex > 0 )      
        lSetOffsetBin += _hash_table_bit_counts_lookup[i][ lBinSectionIndex - 1 ];
      
      assert( lSetOffsetBin > 0 );
      
      lSetOffsetBin -= 1; // to make it an array index rather than a count.
      
      return lSetOffsetBin;
    }    
    
    unsigned int SetOffsetBinToSetOffset( unsigned long long aSetOffsetBin, int i )
    {
      return _set_offsets[i][ aSetOffsetBin ];
    }
    
    Set * SetOffsetToSet( unsigned int aSetOffset )
    {      
      if ( aSetOffset == 0 )
        return NULL;
    
      return *(_sets[ aSetOffset ]);// if this exists, it should always point somewhere valid. 
    }
    
    Set * init_new_set()
    {
      Set * lSet = NULL;//
      if ( _last_set_offset > 65534 ) 
      {
        lSet = GetSmallestExistingSet();
      } 
      else
      {
        lSet = new Set( this, ++_last_set_offset ); 
        _unique_sets.insert( lSet );
        _sets.push_back( lSet->Self );      
      }
      
      return lSet;
    }
    
    Set * GetSmallestExistingSet()
    {
      unsigned long long lSmallestCountSoFar = -1;
      Set * lSmallestSetSoFar = NULL;
      
      for ( set<Set*>::iterator lIt = _unique_sets.begin(); lIt != _unique_sets.end(); ++lIt )
      {
        if ((*lIt)->Count < lSmallestCountSoFar )
        {
          lSmallestCountSoFar = (*lIt)->Count;
          lSmallestSetSoFar = *lIt;
        }
        
        if ( (*lIt)->Count <= _smallest_set_count )
          return *lIt;
      }
      
      _smallest_set_count = lSmallestCountSoFar;
      return lSmallestSetSoFar;
    }
    
    Set * bridge_sets( Set * aEncounteredSet, Set * aOriginatingSet )
    { 
      assert ( aOriginatingSet != aEncounteredSet );

      if ( aOriginatingSet->size() > aEncounteredSet->size() )
      {
        aOriginatingSet->consume( aEncounteredSet );
        return aOriginatingSet;
      }
      else
      {
        aEncounteredSet->consume( aOriginatingSet );
        return aEncounteredSet;
      }
    }
    
    HashIntoType _move_hash_foward( HashIntoType & aOldForwardHash, HashIntoType & aOldReverseHash, const char & aNextNucleotide )
    {
      unsigned long long lTwoBit = twobit_repr( aNextNucleotide );
      
      aOldForwardHash = aOldForwardHash << 2; // left-shift the previous hash over
      aOldForwardHash |= lTwoBit; // 'or' in the current nucleotide
      aOldForwardHash &= bitmask; // mask off the 2 bits we shifted over.
      
      // now handle reverse complement
      aOldReverseHash = aOldReverseHash >> 2;
      aOldReverseHash |= (compl_twobit(lTwoBit) << (_ksize*2 - 2));
      
      // pick the better bin of the forward or reverse hashes
      return uniqify_rc(aOldForwardHash, aOldReverseHash);
    }
        
    virtual unsigned int output_partitioned_file(const std::string infilename,
                                                 const std::string outputfilename,
                                                 CallbackFn callback=0,
                                                 void * callback_data=0)
    {
      IParser* parser = IParser::get_parser(infilename);
      ofstream outfile(outputfilename.c_str());
      
      unsigned int total_reads = 0;
      unsigned int reads_kept = 0;
      
      Read read;
      string seq;
      
      //std::string first_kmer;
      //HashIntoType forward_hash = 0, reverse_hash = 0;
      
      map<unsigned int, unsigned int> lReadCounts;
      
      while(!parser->is_complete()) {
        read = parser->get_next_read();
        seq = read.seq;
        
        
        
        if (check_read(seq)) {
          
          const char * sp = seq.c_str();
          const unsigned int length = seq.length();
          
          //first_kmer = seq.substr(0, _ksize);
          
          // generate the hash for the first kmer in the read (fair amount of work)
          //HashIntoType hash = _hash(first_kmer.c_str(), _ksize, forward_hash, reverse_hash);

          
          HashIntoType forward_hash = 0, reverse_hash = 0;          
          // generate the hash for the first kmer in the read (fair amount of work)
          HashIntoType hash = _hash(sp, _ksize, forward_hash, reverse_hash);
          
          // try for the first hash
          Set * lWorkingSet = SelectOrCreateProperSet( hash );          
          if ( lWorkingSet == NULL ) // couldn't find it, let's keep looking
          {
            // for the rest of the string
            // now, do the rest of the kmers in this read (add one nt at a time)      
            for (unsigned int i = _ksize; i < length; i++) 
            {
              if ( lWorkingSet == NULL )
              {
                HashIntoType next_hash = _move_hash_foward( forward_hash, reverse_hash, sp[i] );             
                lWorkingSet = SelectOrCreateProperSet(next_hash);
              }
              else
                break;
            }
          }
          
          unsigned int lSetID = 0;
          if ( lWorkingSet != NULL )
            lSetID =  lWorkingSet->SetOffset;
            
          lReadCounts[ lSetID ]++;
          

          
          outfile << ">" << read.name << "\t" 
          << lSetID 
          << " " << "\n" 
          << seq << "\n";
          
          // reset the sequence info, increment read number
          total_reads++;
          
          // run callback, if specified
          if (total_reads % CALLBACK_PERIOD == 0 && callback) {
            try {
              callback("do_truncated_partition/output", callback_data,
                       total_reads, reads_kept);
            } catch (...) {
              delete parser; parser = NULL;
              outfile.close();
              throw;
            }
          }
        }
      }
      
      for ( map<unsigned int, unsigned int>::iterator lIt = lReadCounts.begin(); lIt != lReadCounts.end(); ++lIt )
      {
        cout << setw(10) << lIt->first;
        cout << setw(10) << lIt->second << endl;        
      }
      
      cout << setw(6) << "unique set count: "<< lReadCounts.size() << endl;
      cout << endl;
      
      //cout << "Hash Entries: " << _total_unique_hash_count << endl;
      
      delete parser; parser = NULL;
      
      return lReadCounts.size();
    }
    
    bool is_prime( unsigned long long aCandidate )
    {
      if ( aCandidate < 2 )
        return false;
      
      if ( aCandidate == 2 )
        return true;
      
      if ( aCandidate % 2 == 0 )
        return false;
      
      for ( unsigned long long i = 3; i < pow((double)aCandidate, 0.5) + 1; i+= 2 )
      {
        if ( aCandidate % i == 0 )
          return false;
      }
      
      return true;  
    }
    
    unsigned long long get_first_prime_below( unsigned long long aNumber )
    {
      unsigned long long i = aNumber - 1;
      
      if ( i % 2 == 0 ) // no even
        --i;
      
      while ( i > 0 )
      {
        if ( is_prime( i ) )
          return i;
        
        i -= 2;
      }
      
      return 0;
    }
    
    unsigned long long get_first_prime_above( unsigned long long aNumber )
    {
      unsigned long long i = aNumber + 1;
      
      if ( i % 2 == 0 ) // no even
        ++i;
      
      while ( true )
      {
        if ( is_prime( i ) )
          return i;
        
        i += 2;
      }
    }

    // fucking duplicate code drives me nuts. I swear I will clean this up once I get some functionality that I'm happy with.
    void consume_reads(const std::string &filename,
                       unsigned int &total_reads,
                       unsigned long long &n_consumed,
                       ConsumeStringFN consume_string_fn,
                       HashIntoType lower_bound = 0,
                       HashIntoType upper_bound = 0,
                       ReadMaskTable ** orig_readmask = NULL,
                       bool update_readmask = true,
                       CallbackFn callback = NULL,
                       void * callback_data = NULL)
    {
      total_reads = 0;
      n_consumed = 0;
      
      IParser* parser = IParser::get_parser(filename.c_str());
      Read read;
      
      string currName = "";
      string currSeq = "";
      
      //
      // readmask stuff: were we given one? do we want to update it?
      // 
      
      ReadMaskTable * readmask = NULL;
      std::list<unsigned int> masklist;
      
      if (orig_readmask && *orig_readmask) {
        readmask = *orig_readmask;
      }
      
      //
      // iterate through the FASTA file & consume the reads.
      //
      
      while(!parser->is_complete())  {
        read = parser->get_next_read();
        currSeq = read.seq;
        currName = read.name; 
        
        // do we want to process it?
        if (!readmask || readmask->get(total_reads)) {
          
          // yep! process.
          unsigned int this_n_consumed;
          bool is_valid;
          
          this_n_consumed = check_and_process_read(currSeq,
                                                   is_valid,
                                                   consume_string_fn,
                                                   lower_bound,
                                                   upper_bound);
          
          // was this an invalid sequence -> mark as bad?
          if (!is_valid && update_readmask) {
            if (readmask) {
              readmask->set(total_reads, false);
            } else {
              masklist.push_back(total_reads);
            }
          } else {		// nope -- count it!
            n_consumed += this_n_consumed;
          }
        }
        
        // reset the sequence info, increment read number
        total_reads++;
        
        if (total_reads % 10000 == 0)
          cout << total_reads << endl;
        
        // run callback, if specified
        if (total_reads % CALLBACK_PERIOD == 0 && callback) {
          try {
            callback("consume_fasta", callback_data, total_reads, n_consumed);
          } catch (...) {
            throw;
          }
        }
      }
      
      
      //
      // We've either updated the readmask in place, OR we need to create a
      // new one.
      //
      
      if (orig_readmask && update_readmask && readmask == NULL) {
        // allocate, fill in from masklist
        readmask = new ReadMaskTable(total_reads);
        
        list<unsigned int>::const_iterator it;
        for(it = masklist.begin(); it != masklist.end(); ++it) {
          readmask->set(*it, false);
        }
        *orig_readmask = readmask;
      }
    }
    
    //
    // check_and_process_read: checks for non-ACGT characters before consuming
    //
    
    unsigned int check_and_process_read(const std::string &read,
                                                   bool &is_valid,
                                                   ConsumeStringFN consume_string_fn,
                                                   HashIntoType lower_bound,
                                                   HashIntoType upper_bound)
    {
      is_valid = check_read(read);
      
      if (!is_valid) { return 0; }
      
      return (this->*consume_string_fn)(read, lower_bound, upper_bound);
    }
  };
}



#endif //BLEUFILTER_HPP

