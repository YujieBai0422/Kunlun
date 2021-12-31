#ifndef KUNLUN_PSU_HPP_
#define KUNLUN_PSU_HPP_

#include "../crypto/ec_point.hpp"
#include "../crypto/hash.hpp"
#include "../crypto/prg.hpp"
#include "../crypto/block.hpp"
#include "../netio/stream_channel.hpp"
#include "../ot/iknp_ote.hpp"
#include "../filter/bloom_filter.hpp"
#include "../filter/cuckoo_filter.hpp"

/*
** implement PSU based on weak commutative PSU
*/

namespace PSU{

struct PP
{
    bool malicious = false;
    ECPoint g;  
    double desired_false_positive_probability; 
    std::string filter_type; // shuffle, bloom, cuckoo
    size_t statistical_security_parameter; // default=40 
};

void Setup(PP &pp, std::string filter_type, size_t lambda)
{
    pp.g = ECPoint(generator); 
    pp.statistical_security_parameter = lambda; 
    pp.filter_type = filter_type; 
    pp.desired_false_positive_probability = 1/pow(2, pp.statistical_security_parameter/2);
}

void GetNPOTPP(PP &pp, NPOT::PP &pp_npot)
{
    pp_npot.g = pp.g; 
}

void Sender(NetIO &io, PP &pp, std::vector<block> &vec_X, size_t LEN) 
{
    auto start_time = std::chrono::steady_clock::now();  
    
    std::vector<ECPoint> vec_Fk2_Y(LEN);
    io.ReceiveECPoints(vec_Fk2_Y.data(), LEN); 

    BigInt k1 = GenRandomBigIntLessThan(order);
    std::vector<ECPoint> vec_Fk1_X(LEN);
    
    #ifdef THREAD_SAFE
        #pragma omp parallel for
    #endif
    for(auto i = 0; i < LEN; i++){
        vec_Fk1_X[i] = Hash::BlockToECPoint(vec_X[i]) * k1; // H(x_i)^k1
    }
    io.SendECPoints(vec_Fk1_X.data(), LEN); 

    std::cout <<"wcPRF-based PSU [step 2]: Sender ===> F_k1(x_i) ===> Receiver" << std::endl;

    std::vector<ECPoint> vec_Fk1k2_Y(LEN);
    #ifdef THREAD_SAFE
        #pragma omp parallel for
    #endif
    for(auto i = 0; i < LEN; i++){
        vec_Fk1k2_Y[i] = vec_Fk2_Y[i] * k1; 
    }
    // permutation
    std::random_shuffle(vec_Fk1k2_Y.begin(), vec_Fk1k2_Y.end());
    io.SendECPoints(vec_Fk1k2_Y.data(), LEN); 
    std::cout <<"wcPRF-based PSU [step 2]: Sender ===> Permutation(F_k1k2(y_i)) ===> Receiver" << std::endl;

    // send vec_X via one-sided OT
    std::vector<block> vec_dummy(LEN, Block::zero_block);


    IKNPOTE::PP ote_pp; 
    IKNPOTE::Setup(ote_pp); 
    IKNPOTE::Send(io, ote_pp, vec_dummy, vec_X, LEN); 
    std::cout <<"wcPRF-based PSU [step 3]: Sender ===> (X notin Y) ===> Receiver" << std::endl;

    auto end_time = std::chrono::steady_clock::now(); 
    auto running_time = end_time - start_time;
    std::cout << "wcPRF-based PSU: Sender side takes time = " 
              << std::chrono::duration <double, std::milli> (running_time).count() << " ms" << std::endl;
}

void Receiver(NetIO &io, PP &pp, std::vector<block> &vec_Y, size_t LEN, std::unordered_set<std::string> &unionXY)
{
    auto start_time = std::chrono::steady_clock::now(); 
 
    BigInt k2 = GenRandomBigIntLessThan(order);
    std::vector<ECPoint> vec_Fk2_Y(LEN);

    #ifdef THREAD_SAFE
        #pragma omp parallel for
    #endif
    for(auto i = 0; i < LEN; i++){
        vec_Fk2_Y[i] = Hash::BlockToECPoint(vec_Y[i]) * k2; // H(y_i)^k2
    }
    io.SendECPoints(vec_Fk2_Y.data(), LEN); 

    std::cout <<"wcPRF-based PSU [step 1]: Receiver ===> F_k2(y_i) ===> Sender" << std::endl;

    std::vector<ECPoint> vec_Fk1_X(LEN); 
    io.ReceiveECPoints(vec_Fk1_X.data(), LEN); 

    std::vector<ECPoint> vec_Fk1k2_Y(LEN); 
    io.ReceiveECPoints(vec_Fk1k2_Y.data(), LEN); 
    std::unordered_set<ECPoint, ECPointHash> S;
    
    for(auto i = 0; i < LEN; i++){
        S.insert(vec_Fk1k2_Y[i]); 
    }

    std::vector<ECPoint> vec_Fk2k1_X(LEN); 
    // compute the selection bit vector
    std::vector<uint8_t> vec_selection_bit(LEN); 
    #ifdef THREAD_SAFE
        #pragma omp parallel for
    #endif
    for(auto i = 0; i < LEN; i++){ 
        vec_Fk2k1_X[i] = vec_Fk1_X[i]* k2; 
        if(S.find(vec_Fk2k1_X[i]) == S.end()) vec_selection_bit[i] = 1;  
        else vec_selection_bit[i] = 0;
    }
    std::cout <<"wcPRF-based PSU [step 3]: Receiver ===> selection vector ===> Sender" << std::endl;
    // receiver vec_X via one-sided OT
    std::vector<block> vec_X(LEN); 
    IKNPOTE::PP ote_pp; 
    IKNPOTE::Setup(ote_pp); 
    IKNPOTE::Receive(io, ote_pp, vec_X, vec_selection_bit, LEN); 

    // compute the union
    std::string str_dummy = Block::ToString(Block::zero_block); 

    for(auto i = 0; i < LEN; i++){ 
        unionXY.insert(Block::ToString(vec_Y[i]));
        std::string str_block = Block::ToString(vec_X[i]);
        if(str_block != str_dummy){
            unionXY.insert(str_block);
        }
    }
    std::cout <<"wcPRF-based PSU [step 4]: Receiver computes union(X, Y)" << std::endl;   

    auto end_time = std::chrono::steady_clock::now(); 
    auto running_time = end_time - start_time;
    std::cout << "wcPRF-based PSU: Receiver side takes time = " 
              << std::chrono::duration <double, std::milli> (running_time).count() << " ms" << std::endl; 
}

void PipelineSender(NetIO &io, PP &pp, std::vector<block> &vec_X, size_t LEN) 
{
    auto start_time = std::chrono::steady_clock::now(); 

    BigInt k1 = GenRandomBigIntLessThan(order);

    std::vector<ECPoint> vec_Fk1k2_Y(LEN);
    ECPoint Fk2_Y;
    for(auto i = 0; i < LEN; i++){
        io.ReceiveECPoint(Fk2_Y);
        vec_Fk1k2_Y[i] = Fk2_Y * k1; 
    }

    // permutation
    std::random_shuffle(vec_Fk1k2_Y.begin(), vec_Fk1k2_Y.end());
    io.SendECPoints(vec_Fk1k2_Y.data(), LEN); 
    std::cout <<"wcPRF-based PSU [step 2]: Sender ===> Permutation(F_k1k2(y_i)) ===> Receiver" << std::endl;
   
    ECPoint Fk1_X; 
    for(auto i = 0; i < LEN; i++){
        Fk1_X = Hash::BlockToECPoint(vec_X[i]) * k1; // H(y_i)^k2
        io.SendECPoint(Fk1_X); 
    }   
    std::cout <<"wcPRF-based PSU [step 2]: Sender ===> F_k1(x_i) ===> Receiver" << std::endl;

    // send vec_X via one-sided OT
    IKNPOTE::PP ote_pp; 
    IKNPOTE::Setup(ote_pp); 
    IKNPOTE::OnesidedSend(io, ote_pp, vec_X, LEN); 
    std::cout <<"wcPRF-based PSU [step 3]: Sender ===> (X notin Y) ===> Receiver" << std::endl;

    auto end_time = std::chrono::steady_clock::now(); 
    auto running_time = end_time - start_time;
    std::cout << "wcPRF-based PSU: Sender side takes time = " 
              << std::chrono::duration <double, std::milli> (running_time).count() << " ms" << std::endl; 
}

     
void PipelineReceiver(NetIO &io, PP &pp, std::vector<block> &vec_Y, size_t LEN, std::unordered_set<std::string> &unionXY)
{
    auto start_time = std::chrono::steady_clock::now(); 
    
    BigInt k2 = GenRandomBigIntLessThan(order);
    ECPoint Fk2_Y;

    for(auto i = 0; i < LEN; i++){
        Fk2_Y = Hash::BlockToECPoint(vec_Y[i]) * k2; // H(y_i)^k2
        io.SendECPoint(Fk2_Y); 
    }
    
    std::cout <<"wcPRF-based PSU [step 1]: Receiver ===> F_k2(y_i) ===> Sender" << std::endl;

    ECPoint Fk1k2_Y; 
    std::unordered_set<ECPoint, ECPointHash> S;
    
    for(auto i = 0; i < LEN; i++){
        io.ReceiveECPoint(Fk1k2_Y); 
        S.insert(Fk1k2_Y); 
    }

    ECPoint Fk2k1_X; 
    ECPoint Fk1_X; 

    // compute the selection bit vector
    std::vector<uint8_t> vec_selection_bit(LEN); 
    for(auto i = 0; i < LEN; i++){ 
        io.ReceiveECPoint(Fk1_X); 
        Fk2k1_X = Fk1_X * k2; 
        if(S.find(Fk2k1_X) == S.end()) vec_selection_bit[i] = 1;  
        else vec_selection_bit[i] = 0;
    }
    std::cout <<"wcPRF-based PSU [step 3]: Receiver ===> selection vector ===> Sender" << std::endl;
    // receiver vec_X via one-sided OT
    std::vector<block> vec_X; 
    IKNPOTE::PP ote_pp; 
    IKNPOTE::Setup(ote_pp); 
    IKNPOTE::OnesidedReceive(io, ote_pp, vec_X, vec_selection_bit, LEN); 

    // compute the union
    for(auto i = 0; i < LEN; i++)
        unionXY.insert(Block::ToString(vec_Y[i]));

    for(auto i = 0; i < vec_X.size(); i++) 
        unionXY.insert(Block::ToString(vec_X[i]));

    std::cout <<"wcPRF-based PSU [step 4]: Receiver computes union(X, Y)" << std::endl;   

    auto end_time = std::chrono::steady_clock::now(); 
    auto running_time = end_time - start_time;
    std::cout << "wcPRF-based PSU: Receiver side takes time = " 
    << std::chrono::duration <double, std::milli> (running_time).count() << " ms" << std::endl; 
}
 

void ParallelPipelineSender(NetIO &io, PP &pp, std::vector<block> &vec_X, size_t LEN) 
{
    auto start_time = std::chrono::steady_clock::now(); 

    BigInt k1 = GenRandomBigIntLessThan(order);

    std::vector<ECPoint> vec_Fk1k2_Y(LEN);
    std::vector<ECPoint> vec_Fk2_Y(LEN);

    io.ReceiveECPoints(vec_Fk2_Y.data(), LEN);

    std::vector<ECPoint> vec_Fk1_X(LEN); 
    #pragma omp parallel for
    for(auto i = 0; i < LEN; i++){
        vec_Fk1_X[i] = Hash::ThreadSafeBlockToECPoint(vec_X[i]).ThreadSafeMul(k1); // H(y_i)^k2
    }   
    io.SendECPoints(vec_Fk1_X.data(), LEN);
     
    std::cout <<"wcPRF-based PSU [step 2]: Sender ===> F_k1(x_i) ===> Receiver"; 
    std::cout << " [" << POINT_BYTE_LEN*LEN << " bytes]" << std::endl;

    #pragma omp parallel for
    for(auto i = 0; i < LEN; i++){
        vec_Fk1k2_Y[i] = vec_Fk2_Y[i].ThreadSafeMul(k1); 
    }

    // permutation
    if(pp.filter_type == "shuffle"){
        std::random_shuffle(vec_Fk1k2_Y.begin(), vec_Fk1k2_Y.end());
        io.SendECPoints(vec_Fk1k2_Y.data(), LEN); 
        std::cout <<"wcPRF-based PSU [step 2]: Sender ===> Permutation(F_k1k2(y_i)) ===> Receiver"; 
        std::cout << " [" << POINT_BYTE_LEN*LEN << " bytes]" << std::endl;
    }

    // generate and send bloom filter
    if(pp.filter_type == "bloom"){
        BloomFilter filter(vec_Fk1k2_Y.size(), pp.desired_false_positive_probability);
        filter.Insert(vec_Fk1k2_Y);
        size_t filter_size = filter.ObjectSize(); 
        io.SendInteger(filter_size);

        char *buffer = new char[filter_size]; 
        filter.WriteObject(buffer);
        io.SendBytes(buffer, filter_size); 
        std::cout <<"wcPRF-based PSU [step 2]: Sender ===> BloomFilter(F_k1k2(y_i)) ===> Receiver";
        std::cout << " [" << filter_size << " bytes]" << std::endl;
        delete[] buffer; 
    } 

    // generate and send cuckoo filter
    if(pp.filter_type == "cuckoo"){
        CuckooFilter filter(vec_Fk1k2_Y.size(), pp.desired_false_positive_probability);
        if(filter.Insert(vec_Fk1k2_Y) == false){
            std::cerr << "cuckoo filter insert fails" << std::endl;
            exit(EXIT_FAILURE); 
        }
        size_t filter_size = filter.ObjectSize(); 
        io.SendInteger(filter_size);

        char *buffer = new char[filter_size]; 
        filter.WriteObject(buffer);
        io.SendBytes(buffer, filter_size); 
        std::cout <<"wcPRF-based PSU [step 2]: Sender ===> CuckooFilter(F_k1k2(y_i)) ===> Receiver";
        std::cout << " [" << filter.ObjectSize() << " bytes]" << std::endl;
        delete[] buffer; 
    } 

    //send vec_X via one-sided OT
    IKNPOTE::PP ote_pp; 
    IKNPOTE::Setup(ote_pp); 
    IKNPOTE::OnesidedSend(io, ote_pp, vec_X, LEN); 
    std::cout <<"wcPRF-based PSU [step 3]: Sender ===> (X notin Y) ===> Receiver" << std::endl; 

    auto end_time = std::chrono::steady_clock::now(); 
    auto running_time = end_time - start_time;
    std::cout << "wcPRF-based PSU: Sender side takes time = " 
        << std::chrono::duration <double, std::milli> (running_time).count() << " ms" << std::endl;
}

     
void ParallelPipelineReceiver(NetIO &io, PP &pp, std::vector<block> &vec_Y, size_t LEN, std::unordered_set<std::string> &unionXY)
{
    auto start_time = std::chrono::steady_clock::now(); 
    
    BigInt k2 = GenRandomBigIntLessThan(order);
    std::vector <ECPoint> vec_Fk2_Y(LEN);
    #pragma omp parallel for
    for(auto i = 0; i < LEN; i++){
        vec_Fk2_Y[i] = Hash::ThreadSafeBlockToECPoint(vec_Y[i]).ThreadSafeMul(k2); // H(y_i)^k2
    }

    io.SendECPoints(vec_Fk2_Y.data(), LEN); 
    
    std::cout <<"wcPRF-based PSU [step 1]: Receiver ===> F_k2(y_i) ===> Sender";
    std::cout << " [" << POINT_BYTE_LEN*LEN << " bytes]" << std::endl;

    std::vector<ECPoint> vec_Fk2k1_X(LEN); 
    std::vector<ECPoint> vec_Fk1_X(LEN); 


    io.ReceiveECPoints(vec_Fk1_X.data(), LEN);  
    #pragma omp parallel for
    for(auto i = 0; i < LEN; i++){ 
        vec_Fk2k1_X[i] = vec_Fk1_X[i].ThreadSafeMul(k2); 
    }

    // compute the selection bit vector
    std::vector<uint8_t> vec_selection_bit(LEN);

    if(pp.filter_type == "shuffle"){
        std::vector<ECPoint> vec_Fk1k2_Y(LEN);
        io.ReceiveECPoints(vec_Fk1k2_Y.data(), LEN);
        std::unordered_set<ECPoint, ECPointHash> S;
        for(auto i = 0; i < LEN; i++){
            S.insert(vec_Fk1k2_Y[i]); 
        }
        for(auto i = 0; i < LEN; i++){
            if(S.find(vec_Fk2k1_X[i]) == S.end()) vec_selection_bit[i] = 1;  
            else vec_selection_bit[i] = 0;
        }
    }

    if(pp.filter_type == "bloom"){
        BloomFilter filter; 
        size_t filter_size = filter.ObjectSize();
        io.ReceiveInteger(filter_size);

        char *buffer = new char[filter_size]; 
        io.ReceiveBytes(buffer, filter_size);
          
        filter.ReadObject(buffer);  
        delete[] buffer; 

        #pragma omp parallel for
        for(auto i = 0; i < LEN; i++){
            if(filter.Contain(vec_Fk2k1_X[i]) == false) vec_selection_bit[i] = 1;  
            else vec_selection_bit[i] = 0;
        }
    } 

    if(pp.filter_type == "cuckoo"){
        CuckooFilter filter; 

        size_t filter_size; 
        io.ReceiveInteger(filter_size);

        char *buffer = new char[filter_size]; 
        io.ReceiveBytes(buffer, filter_size);
          
        filter.ReadObject(buffer);  
        delete[] buffer; 

        #pragma omp parallel for
        for(auto i = 0; i < LEN; i++){
            if(filter.Contain(vec_Fk2k1_X[i]) == false) vec_selection_bit[i] = 1;  
            else vec_selection_bit[i] = 0;
        }
    } 
     
    std::cout <<"wcPRF-based PSU [step 3]: Receiver ===> selection vector ===> Sender" << std::endl;
    // receiver vec_X via one-sided OT
    std::vector<block> vec_X; 
    IKNPOTE::PP ote_pp; 
    IKNPOTE::Setup(ote_pp); 
    IKNPOTE::OnesidedReceive(io, ote_pp, vec_X, vec_selection_bit, LEN); 

    // compute the union
    for(auto i = 0; i < LEN; i++)
        unionXY.insert(Block::ToString(vec_Y[i]));

    for(auto i = 0; i < vec_X.size(); i++) 
        unionXY.insert(Block::ToString(vec_X[i]));

    std::cout <<"wcPRF-based PSU [step 4]: Receiver computes union(X,Y)" << std::endl;    

    auto end_time = std::chrono::steady_clock::now(); 
    auto running_time = end_time - start_time;
    std::cout << "wcPRF-based PSU: Receiver side takes time = " 
        << std::chrono::duration <double, std::milli> (running_time).count() << " ms" << std::endl;
}



}
#endif