#include "../mpc/pso/mqrpmt_psi_card.hpp"
#include "../crypto/setup.hpp"


struct TestCase{
    size_t LOG_SENDER_LEN; 
    size_t LOG_RECEIVER_LEN; 
    size_t SENDER_LEN; 
    size_t RECEIVER_LEN; 
    std::vector<block> vec_X; // sender's set
    std::vector<block> vec_Y; // receiver's set

    size_t HAMMING_WEIGHT; // cardinality of intersection
    std::vector<uint8_t> vec_indication_bit; // X[i] = Y[i] iff b[i] = 1 
};

// LEN is the cardinality of two sets
TestCase GenTestCase(size_t LOG_SENDER_LEN, size_t LOG_RECEIVER_LEN)
{
    TestCase testcase;

    testcase.LOG_SENDER_LEN = LOG_SENDER_LEN; 
    testcase.LOG_RECEIVER_LEN = LOG_RECEIVER_LEN; 
    testcase.SENDER_LEN = size_t(pow(2, testcase.LOG_SENDER_LEN));  
    testcase.RECEIVER_LEN = size_t(pow(2, testcase.LOG_RECEIVER_LEN)); 

    PRG::Seed seed = PRG::SetSeed(nullptr, 0); // initialize PRG
    testcase.vec_X = PRG::GenRandomBlocks(seed, testcase.SENDER_LEN);
    testcase.vec_Y = PRG::GenRandomBlocks(seed, testcase.RECEIVER_LEN);

    // set the Hamming weight to be a half of the max possible intersection size
    testcase.HAMMING_WEIGHT = std::min(testcase.SENDER_LEN, testcase.RECEIVER_LEN)/2;

    // generate a random indication bit vector conditioned on given Hamming weight
    testcase.vec_indication_bit.resize(testcase.SENDER_LEN);  
    for(auto i = 0; i < testcase.SENDER_LEN; i++){
        if(i < testcase.HAMMING_WEIGHT) testcase.vec_indication_bit[i] = 1; 
        else testcase.vec_indication_bit[i] = 0; 
    }
    std::shuffle(testcase.vec_indication_bit.begin(), testcase.vec_indication_bit.end(), global_built_in_prg);

    // adjust vec_X and vec_Y
    for(auto i = 0, j = 0; i < testcase.SENDER_LEN; i++){
        if(testcase.vec_indication_bit[i] == 1){
            testcase.vec_X[i] = testcase.vec_Y[j];
            j++; 
        }
    }

    std::shuffle(testcase.vec_Y.begin(), testcase.vec_Y.end(), global_built_in_prg);

    return testcase; 
}

void PrintTestCase(TestCase testcase)
{
    PrintSplitLine('-'); 
    std::cout << "TESTCASE INFO >>>" << std::endl;
    std::cout << "Sender's set size = " << testcase.SENDER_LEN << std::endl;
    std::cout << "Receiver's set size = " << testcase.RECEIVER_LEN << std::endl;
    std::cout << "Intersection cardinality = " << testcase.HAMMING_WEIGHT << std::endl; 
    PrintSplitLine('-'); 
}

void SaveTestCase(TestCase &testcase, std::string testcase_filename)
{
    std::ofstream fout; 
    fout.open(testcase_filename, std::ios::binary); 
    if(!fout)
    {
        std::cerr << testcase_filename << " open error" << std::endl;
        exit(1); 
    }

    fout << testcase.LOG_SENDER_LEN; 
    fout << testcase.LOG_RECEIVER_LEN; 
    fout << testcase.SENDER_LEN; 
    fout << testcase.RECEIVER_LEN; 
    fout << testcase.HAMMING_WEIGHT; 
     
    fout << testcase.vec_X; 
    fout << testcase.vec_Y; 
    fout << testcase.vec_indication_bit;

    fout.close(); 
}

void FetchTestCase(TestCase &testcase, std::string testcase_filename)
{
    std::ifstream fin; 
    fin.open(testcase_filename, std::ios::binary); 
    if(!fin)
    {
        std::cerr << testcase_filename << " open error" << std::endl;
        exit(1); 
    }

    fin >> testcase.LOG_SENDER_LEN; 
    fin >> testcase.LOG_RECEIVER_LEN; 
    fin >> testcase.SENDER_LEN; 
    fin >> testcase.RECEIVER_LEN;
    fin >> testcase.HAMMING_WEIGHT; 

    testcase.vec_X.resize(testcase.SENDER_LEN); 
    testcase.vec_Y.resize(testcase.RECEIVER_LEN); 
    testcase.vec_indication_bit.resize(testcase.SENDER_LEN); 

    fin >> testcase.vec_X; 
    fin >> testcase.vec_Y; 
    fin >> testcase.vec_indication_bit;

    fin.close(); 
}

int main()
{
    CRYPTO_Initialize(); 

    std::cout << "mqRPMT-based PSI-card test begins >>>" << std::endl; 

    PrintSplitLine('-');  
    std::cout << "generate or load public parameters and test case" << std::endl;

    // generate pp (must be same for both server and client)
    std::string pp_filename = "mqRPMTPSIcard.pp"; 
    mqRPMTPSIcard::PP pp;   
    if(!FileExist(pp_filename)){
        std::cout << pp_filename << " does not exist" << std::endl;
        std::string filter_type = "bloom"; 
        size_t computational_security_parameter = 128;         
        size_t statistical_security_parameter = 40; 
        size_t LOG_SENDER_LEN = 20;
        size_t LOG_RECEIVER_LEN = 20;  
        pp = mqRPMTPSIcard::Setup("bloom", computational_security_parameter, statistical_security_parameter, 
                              LOG_SENDER_LEN, LOG_RECEIVER_LEN); 
        mqRPMTPSIcard::SavePP(pp, pp_filename); 
    }
    else{
        std::cout << pp_filename << " already exists" << std::endl;
        mqRPMTPSIcard::FetchPP(pp, pp_filename); 
    }

    std::string testcase_filename = "mqRPMTPSIcard.testcase"; 
    
    // generate test instance (must be same for server and client)
    TestCase testcase; 
    if(!FileExist(testcase_filename)){ 
        std::cout << testcase_filename << " does not exist" << std::endl;
        testcase = GenTestCase(pp.LOG_SENDER_LEN, pp.LOG_RECEIVER_LEN); 
        SaveTestCase(testcase, testcase_filename); 
    }
    
    else{
        std::cout << testcase_filename << " already exists" << std::endl;
        FetchTestCase(testcase, testcase_filename);
        if((testcase.LOG_SENDER_LEN != pp.LOG_SENDER_LEN) || (testcase.LOG_SENDER_LEN != pp.LOG_SENDER_LEN)){
            std::cerr << "testcase and public parameter do not match" << std::endl; 
        }
    }
    PrintTestCase(testcase); 

    std::string party;
    std::cout << "please select your role between sender and receiver (hint: first start receiver, then start sender) ==> "; 

    std::getline(std::cin, party);
    PrintSplitLine('-'); 

    if(party == "sender"){
        NetIO client("client", "127.0.0.1", 8080);        
        mqRPMTPSIcard::Send(client, pp, testcase.vec_X);
    } 

    if(party == "receiver"){
        NetIO server("server", "", 8080);
        size_t HAMMING_WEIGHT_prime = mqRPMTPSIcard::Receive(server, pp, testcase.vec_Y);

        std::cout << "Intersection cardinality (test) = " << HAMMING_WEIGHT_prime << std::endl;

        double error_probability = abs(double(testcase.HAMMING_WEIGHT)-double(HAMMING_WEIGHT_prime))/double(testcase.HAMMING_WEIGHT); 
        std::cout << "mqRPMT-based PSI-card test succeeds with probability " << (1 - error_probability) << std::endl; 
    }

    CRYPTO_Finalize();   
    
    return 0; 
}