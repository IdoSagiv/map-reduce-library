#include "MapReduceFramework.h"
#include <cstdio>
#include <string.h>
#include <array>
#include <fstream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <bits/stdc++.h>

#include <errno.h>
#include <dirent.h>
#include <sys/time.h>

#include<map>

extern int errno;

pthread_mutex_t k2ResourcesMutex = PTHREAD_MUTEX_INITIALIZER;

class VPath : public V1 {
public:
    VPath(std::string path) : path(path) {}

    std::string path;
};

class KWord : public K2, public K3 {
public:
    KWord(std::string s) : s(s) {}

    virtual bool operator<(const K2 & other) const {
        return s < static_cast<const KWord &>(other).s;
    }

    virtual bool operator<(const K3 & other) const {
        return s < static_cast<const KWord &>(other).s;
    }

    std::string s;
};

class VCount : public V2, public V3 {
public:
    VCount(int count) : count(count) {}

    int count;
};


class CounterClient : public MapReduceClient {
public:
    std::vector<KWord *> *resourcesK2;
    std::vector<VCount *> *resourcesV2;

    /**
     * Constructor
     */
    CounterClient() {
        resourcesK2 = new std::vector<KWord *>;
        resourcesV2 = new std::vector<VCount *>;
    }

    /**
     * Destructor
     */
    ~CounterClient() {
        while (!resourcesK2->empty()) {
            delete resourcesK2->at(0);
            resourcesK2->erase(resourcesK2->begin());
        }
        delete resourcesK2;

        while (!resourcesV2->empty()) {
            delete resourcesV2->at(0);
            resourcesV2->erase(resourcesV2->begin());
        }
        delete resourcesV2;
    }

    /**
     * maps the given file to pairs (word,count) where count is the number of times the word appeared in the file.
     * @param key ignored.
     * @param value path to file
     * @param context information on the current thread, given by the framework
     */
    void map(const K1 *key, const V1 *value, void *context) const {
        std::map<std::string, int> counts;
        std::vector<std::string> words;
        std::string fileContent;
        std::string path = static_cast<const VPath *>(value)->path;

        loadFile(path, fileContent);
        tokenize(fileContent, ' ', words);


        for ( auto word = words.begin(); word != words.end(); ++word ) {
            auto lb = counts.lower_bound(*word);
            if (lb != counts.end() && !(counts.key_comp()(*word, lb->first))) {
                lb->second++;
            }
            else {
                counts.insert(lb, std::make_pair(*word, 1));
            }
        }

        for ( auto itr = counts.begin(); itr != counts.end(); ++itr ) {
            KWord *k2 = new KWord(itr->first);
            VCount *v2 = new VCount(itr->second);

            pthread_mutex_lock(&k2ResourcesMutex);
            resourcesK2->push_back(k2);
            resourcesV2->push_back(v2);
            pthread_mutex_unlock(&k2ResourcesMutex);

            emit2(k2, v2, context);
        }
    }

    /**
     * reduce the given word and vector of counts to a single pair (word,count) where count is the sum of all
     * counts in the vector, i.e the total number of times the word appeared in the files.
     * @param key a word
     * @param values vector of number of appearances in the files (each entry is count in a specific file)
     * @param context
     */
    void reduce(const K2 *key, const std::vector<V2 *> & values, void *context) const {
        const std::string s = static_cast<const KWord *>(key)->s;
        int count = 0;
        for ( V2 *val: values ) {
            count += static_cast<const VCount *>(val)->count;
        }
        KWord *k3 = new KWord(s);
        VCount *v3 = new VCount(count);
        emit3(k3, v3, context);
    }

private:
    int loadFile(std::string & path, std::string & content) const {
        std::fstream file;
        file.open(path.c_str());
        if (file.is_open()) {
            std::getline(file, content, '\0');
            return !file.bad();
        }
        return 0;
    }

    void tokenize(std::string & str, const char delim, std::vector<std::string> & out) const {
        int occurances = 1;
        int delim_count = 0;
        std::string token;
        std::string currentWord;
        std::string::iterator c = str.begin();
        while (c != str.end()) {
            if (*c == delim) {
                delim_count++;
                if (delim_count == occurances) {
                    out.push_back(currentWord);
                    currentWord = "";

                    //restart the search
                    c = str.begin();
                    occurances++;
                    delim_count = 0;
                }
                else {
                    currentWord = "";
                }
            }
            else {
                currentWord += *c;
            }
            c++;
        }
        out.push_back(currentWord);
    }
};

/**
 *
 * @param argc - num of arguments passed to the program.
 * @param argv - the arguments that passed to the program.
 * @param in - input vector to update - vector of the file paths in the input dir.
 * @param paths - vector of the file paths in the input dir (for further de-allocation of the input vector).
 * @param thread_num reference to the thread_num variable to update.
 * @return -1 iff failed.
 */
int parse_input(int argc, char **argv, InputVec *in, std::vector<VPath *> *paths, int & thread_num) {
    // check the correct number of arguments was given
    if (argc != 3) {
        printf("usage: [directory_name] [thread_num]");
        return -1;
    }

    thread_num = atol(argv[2]);
    struct dirent *entry = nullptr;
    DIR *dp = nullptr;
    dp = opendir(argv[1]);
    if (dp == nullptr) {
        printf("fail to open directory\n");
        return -1;
    }

    struct stat path_stat;
    // path max len is 256
    char path[256 + 1];
    sprintf(path, "%s", argv[1]);
    int dirname_len = strlen(argv[1]);
    while ((entry = readdir(dp))) {
        sprintf(path + dirname_len, "/%s", entry->d_name);

        if (stat(path, &path_stat) < 0) {
            printf("stat fail on %s: %d\n", path, errno);
            continue;
        }

        if (S_ISREG(path_stat.st_mode)) {
            VPath *v = new VPath(path);
            paths->push_back(v);
            in->push_back({nullptr, v});
        }
    }

    return closedir(dp);
}

/**
 * the usage of the program should be: <directory_name> <thread_num>
 * @param argc
 * @param argv
 * @return
 */
int main(int argc, char **argv) {
    InputVec inputVec;
    std::vector<VPath *> paths;
    int thread_num;
    if (parse_input(argc, argv, &inputVec, &paths, thread_num) < 0) {
        printf("FAIL\n");
        exit(-1);
    }
    CounterClient client;
    OutputVec outputVec;

    JobState state;
    JobState last_state = {UNDEFINED_STAGE, 0};

    // start the map-reduce process
    JobHandle job = startMapReduceJob(client, inputVec, outputVec, thread_num);
    getJobState(job, &state);

    // repeat until the end of the reduce stage
    while (state.stage != REDUCE_STAGE || state.percentage != 100.0) {
        // print progress if it was updated
        if (last_state.stage != state.stage || last_state.percentage != state.percentage) {
            printf("stage %d, %.2f %% \n", state.stage, state.percentage);
        }

        last_state = state;
        getJobState(job, &state);
    }
    // print progress
    printf("stage %d, %.2f %% \n", state.stage, state.percentage);

    closeJobHandle(job);
    printf("Done to Map Reduce!\n");


    // print results and delete allocations
    printf("Results:\n");
    for ( OutputPair & pair: outputVec ) {
        std::string s = ((const KWord *) pair.first)->s;
        int count = ((const VCount *) pair.second)->count;
        printf("The word %s appeared %d time%s\n", s.c_str(), count, count > 1 ? "s" : "");
        delete pair.first;
        delete pair.second;
    }

    // de-allocate the VPath that allocated for the input vector
    for ( unsigned int i = 0; i < paths.size(); i++ ) {
        delete paths[i];
    }

    // success
    return 0;
}

