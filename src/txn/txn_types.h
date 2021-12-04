#ifndef _TXN_TYPES_H_
#define _TXN_TYPES_H_

#include <map>
#include <set>
#include <string>

#include "txn.h"

// Immediately commits.
class Noop : public Txn
{
   public:
    Noop() {}
    virtual void Run() { COMMIT; }
    Noop* clone() const
    {  // Virtual constructor (copying)
        Noop* clone = new Noop();
        this->CopyTxnInternals(clone);
        return clone;
    }
};

// Reads all keys in the map 'm', if all results correspond to the values in
// the provided map, commits, else aborts.

/* TODO: Modify Expect to take in a second parameter k, where 0 ≤ k < n. Expect
   will always acreate a read_set that contains keys across k partitions.       */

class Expect : public Txn
{
   public:
    Expect(const map<Key, Value>& m) : m_(m)
    {
        for (map<Key, Value>::iterator it = m_.begin(); it != m_.end(); ++it) readset_.insert(it->first);
    }

    Expect(const set<Key> s)
    {
        std::map<Key,Value> m;
        std::transform(s.cbegin(), s.cend(), std::inserter(m, begin(m)), [] (const Key &arg) { return std::make_pair(arg, 1);});
        m_ = m;
        for (set<Key>::iterator it = s.begin(); it != s.end(); ++it) readset_.insert(*it);
    }

    Expect* clone() const
    {  // Virtual constructor (copying)
        Expect* clone = new Expect(map<Key, Value>(m_));
        this->CopyTxnInternals(clone);
        return clone;
    }

    virtual void Run()
    {
        Value result;
        for (map<Key, Value>::iterator it = m_.begin(); it != m_.end(); ++it)
        {
            if (!Read(it->first, &result) || result != it->second)
            {
                // TODO: Remove abort
                ABORT;
            }
        }
        COMMIT;
    }

   private:
    map<Key, Value> m_;
};

// Inserts all pairs in the map 'm'.
class Put : public Txn
{
   public:
    Put(const map<Key, Value>& m) : m_(m)
    {
        for (map<Key, Value>::iterator it = m_.begin(); it != m_.end(); ++it) writeset_.insert(it->first);
    }

    Put* clone() const
    {  // Virtual constructor (copying)
        Put* clone = new Put(map<Key, Value>(m_));
        this->CopyTxnInternals(clone);
        return clone;
    }

    virtual void Run()
    {
        for (map<Key, Value>::iterator it = m_.begin(); it != m_.end(); ++it) Write(it->first, it->second);
        COMMIT;
    }

   private:
    map<Key, Value> m_;
};

// Read-modify-write transaction.

/* TODO: Modify RMW to take in a new parameter k, where 0 ≤ k < n. Expect
   will always acreate a read_set that contains keys across k partitions        */

class RMW : public Txn
{
   public:
    explicit RMW(double time = 0) : time_(time) {}
    RMW(const set<Key>& writeset, double time = 0) : time_(time) { writeset_ = writeset; }
    RMW(const set<Key>& readset, const set<Key>& writeset, double time = 0) : time_(time)
    {
        readset_  = readset;
        writeset_ = writeset;
    }

    // Constructor with randomized read/write sets
    RMW(int dbsize, int readsetsize, int writesetsize, double time = 0) : time_(time)
    {
        // Make sure we can find enough unique keys.
        DCHECK(dbsize >= readsetsize + writesetsize);

        // Find readsetsize unique read keys.
        for (int i = 0; i < readsetsize; i++)
        {
            Key key;
            do
            {
                key = rand() % dbsize;
            } while (readset_.count(key));
            readset_.insert(key);
        }

        // Find writesetsize unique write keys.
        for (int i = 0; i < writesetsize; i++)
        {
            Key key;
            do
            {
                key = rand() % dbsize;
            } while (readset_.count(key) || writeset_.count(key));
            writeset_.insert(key);
        }
    }

    // Creates a RMW across k partitions. readsetsize + writesetsize must be >= k. 
    RMW(int dbsize, int readsetsize, int writesetsize, int k, int thread_count,  double time = 0) : time_(time)
    {
        /*
            if k == 1 then
                single site
                if no readset then
                    it is like put
                else
                    it is like expect
            else
                multi-partition transaction

        */

        // Make sure the max partitions requested is <= thread_count
        if (k > thread_count) {
            k = thread_count;
        }

        // Make sure we can find enough unique keys.
        DCHECK(dbsize >= readsetsize + writesetsize);
    
        int chunk_size = dbsize / thread_count;
        
        // Create set with k different values. 
        set<int> partitions;
        vector<int> v;
        int counter = 0;
        while (counter < k) 
        {
            int key = rand() % dbsize;
            int index = key / chunk_size;

            if (partitions.find(index) == partitions.end()) 
            {
                v.push_back(index);
                partitions.insert(index);
                counter++;
            }
        }

        // Used to make sure all partitions are used before inserting into random partition
        int count_across = 0;

        // Find writesetsize unique write keys.
        for (int i = 0; i < writesetsize; i++)
        {
            Key key;

            if (count_across < k)
            {
                // makes sure each partition gets at least one write
                int index = v.at(i);
                do 
                {
                    key = (rand() % chunk_size) + (index * chunk_size);
                } while (readset_.count(key) || writeset_.count(key));
                writeset_.insert(key);

                count_across++;
            } 
            else 
            {

                // Chooses a random partition and adds a value in the partition chosen
                int index = v.at(rand() % k);
                do
                {
                    key = (rand() % chunk_size) + (index * chunk_size);
                } while (readset_.count(key) || writeset_.count(key));
                writeset_.insert(key);
            }
        }

        // Find readsetsize unique read keys.
        for (int i = 0; i < readsetsize; i++)
        {
            Key key;
            if (count_across < k)
            {
                // makes sure each partition gets a read/write before randomizing inserts
                int index = v.at(i);
                do 
                {
                    key = (rand() % chunk_size) + (index * chunk_size);
                } while (readset_.count(key) || writeset_.count(key));
                readset_.insert(key);

                count_across++;
            } 
            else 
            {
                // Chooses a random partition and adds a value in the partition chosen
                int index = v.at(rand() % k);
                do
                {
                    key = (rand() % chunk_size) + (index * chunk_size);
                } while (readset_.count(key) || writeset_.count(key));

                readset_.insert(key);
            }
            
        }

        v.clear();
        partitions.clear();
    }

    RMW* clone() const
    {  // Virtual constructor (copying)
        RMW* clone = new RMW(time_);
        this->CopyTxnInternals(clone);
        return clone;
    }

    virtual void Run()
    {
        Value result;
        // Read everything in readset.
        for (set<Key>::iterator it = readset_.begin(); it != readset_.end(); ++it) Read(*it, &result);

        // Run while loop to simulate the txn logic(duration is time_).
        double begin = GetTime();
        while (GetTime() - begin < time_)
        {
            for (int i = 0; i < 1000; i++)
            {
                int x = 100;
                x     = x + 2;
                x     = x * x;
            }
        }

        // Increment length of everything in writeset.
        for (set<Key>::iterator it = writeset_.begin(); it != writeset_.end(); ++it)
        {
            result = 0;
            Read(*it, &result);
            Write(*it, result + 1);
        }

        COMMIT;
    }

   private:
    double time_;
};

#endif  // _TXN_TYPES_H_
