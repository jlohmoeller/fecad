#include <grpcpp/create_channel.h>

#include <pdclient/config/CommandLineParameters.hpp>
#include <pdclient/entity/Client.hpp>
#include <pdshared/grpc/CostMetricInterceptor.hpp>
#include <pdshared/measure/ResultWriter.hpp>
#include <pdshared/measure/Timer.hpp>
#include <pdshared/util/Utilities.hpp>

int main(const int argc, char **argv)
{
    // Initialization
    pat_disc::Timer::GetInstance().Initialize("Setup");
    pat_disc::client::CommandLineParameters::GetInstance().Parse(argc, argv);
    pat_disc::MatchableAttribute::Parse(pat_disc::client::CommandLineParameters::GetInstance().GetAttributeConfigFile());
    pat_disc::Logger::Initialize("Client", pat_disc::client::CommandLineParameters::GetInstance().GetRunId());

    grpc::ChannelArguments args;
    args.SetMaxReceiveMessageSize(50 * 1024 * 1024);

    std::vector<std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface> > creators;
    creators.push_back(std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface>(new pat_disc::ClientCostMetricInterceptorFactory()));

    auto credOptions = grpc::SslCredentialsOptions();
    credOptions.pem_root_certs = pat_disc::ReadFileContents("data/certificates/ca-cert.pem");
    credOptions.pem_private_key = pat_disc::ReadFileContents("data/certificates/client-key.pem");
    credOptions.pem_cert_chain = pat_disc::ReadFileContents("data/certificates/client-cert.pem");
    const auto channelCredentials = SslCredentials(credOptions);
    const auto channel = CreateCustomChannelWithInterceptors("localhost:50002", channelCredentials, args, std::move(creators));

    pat_disc::Timer::GetInstance().Switch("Context Initialization");
    pat_disc::Client c("Test Hospital", channel);
    c.Initialize();
    pat_disc::Timer::GetInstance().Switch("Idle");

    // Upload patient data
    uint64_t patientCount = 0;
    if (pat_disc::client::CommandLineParameters::GetInstance().HasPatientDataFile())
    {
        const int32_t iterations = pat_disc::client::CommandLineParameters::GetInstance().GetUploadIterations();
        for (int32_t i = 0; i < iterations; i++)
        {
            patientCount += c.AddPatientData(pat_disc::client::CommandLineParameters::GetInstance().GetPatientDataFile());
        }
    }

    // Execute query
    pat_disc::Timer::GetInstance().Switch("Query Execution");
    c.ExecuteQuery(pat_disc::client::CommandLineParameters::GetInstance().GetQueryFile());

    // Shutdown and result persistence
    pat_disc::Timer::GetInstance().Shutdown();

    std::string patientDataHash;
    pat_disc::CalculateFileHash(pat_disc::client::CommandLineParameters::GetInstance().GetPatientDataFile(), patientDataHash);

    pat_disc::ResultWriterInfo info;
    info.entity = "Client";
    info.testId = pat_disc::client::CommandLineParameters::GetInstance().GetTestId();
    info.runId = pat_disc::client::CommandLineParameters::GetInstance().GetRunId();
    info.attributeConfig = pat_disc::client::CommandLineParameters::GetInstance().GetAttributeConfigFile();
    info.query = pat_disc::client::CommandLineParameters::GetInstance().GetQueryFile();
    info.patientDataHash = patientDataHash;
    info.patientCount = patientCount;
    info.plaintext = pat_disc::client::CommandLineParameters::GetInstance().IsPlaintext();
    info.maxRamUsage = pat_disc::GetMaxRamUsageOfProcess();

    pat_disc::ResultWriter::WriteResults(info);
    c.Shutdown();

    pat_disc::Logger::Shutdown();
}
