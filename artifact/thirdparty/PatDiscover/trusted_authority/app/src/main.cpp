#include <pdta/service/TrustedAuthorityService.hpp>

#include <grpc++/server_builder.h>
#include <pdshared/grpc/CostMetricInterceptor.hpp>
#include <pdshared/measure/CommunicationCost.hpp>
#include <pdshared/measure/Timer.hpp>
#include <pdshared/util/Utilities.hpp>
#include <pdta/config/CommandLineParameters.hpp>
#include <pdta/entity/TrustedAuthority.hpp>

int main(const int argc, char **argv)
{
    // Initialization
    pat_disc::Timer::GetInstance().Initialize("Setup");
    pat_disc::CommunicationCost::GetInstance().SwitchLabel("Context Initialization");
    pat_disc::ta::CommandLineParameters::GetInstance().Parse(argc, argv);
    pat_disc::MatchableAttribute::Parse(pat_disc::ta::CommandLineParameters::GetInstance().GetAttributeConfigFile());
    pat_disc::Logger::Initialize("TA", pat_disc::ta::CommandLineParameters::GetInstance().GetRunId());

    // Generate keys
    pat_disc::Timer::GetInstance().Initialize("Context Generation");
    if (!pat_disc::ta::CommandLineParameters::GetInstance().IsUseCachedData())
    {
        pat_disc::TrustedAuthority::Initialize();
    }
    pat_disc::Timer::GetInstance().Switch("Idle");

    pat_disc::TrustedAuthorityService service;

    std::vector<std::unique_ptr<grpc::experimental::ServerInterceptorFactoryInterface> > creators;
    creators.push_back(std::unique_ptr<grpc::experimental::ServerInterceptorFactoryInterface>(new pat_disc::ServerCostMetricInterceptorFactory()));

    auto credOptions = grpc::SslServerCredentialsOptions(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    credOptions.pem_key_cert_pairs.push_back({pat_disc::ReadFileContents("data/certificates/ta-key.pem"), pat_disc::ReadFileContents("data/certificates/ta-cert.pem")});
    credOptions.pem_root_certs = pat_disc::ReadFileContents("data/certificates/ca-cert.pem");

    const auto serverCredentials = SslServerCredentials(credOptions);

    // Start server
    grpc::ServerBuilder builder;
    builder.AddListeningPort("0.0.0.0:50001", serverCredentials);
    builder.RegisterService(&service);
    builder.experimental().SetInterceptorCreators(std::move(creators));

    PD_TRACE("Starting server...");
    const std::unique_ptr server(builder.BuildAndStart());
    server->Wait();
}
