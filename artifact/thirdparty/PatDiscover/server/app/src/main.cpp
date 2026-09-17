#include <grpc++/server_builder.h>

#include <pdserver/config/CommandLineParameters.hpp>
#include <pdserver/entity/Server.hpp>
#include <pdserver/grpc/PatientDataServerService.hpp>
#include <pdshared/grpc/CostMetricInterceptor.hpp>
#include <pdshared/measure/Timer.hpp>
#include <pdshared/util/Utilities.hpp>

int main(const int argc, char **argv)
{
    // Initialization
    pat_disc::Timer::GetInstance().Initialize("Setup");
    pat_disc::server::CommandLineParameters::GetInstance().Parse(argc, argv);
    pat_disc::Logger::Initialize("Server", pat_disc::server::CommandLineParameters::GetInstance().GetRunId());

    pat_disc::MatchableAttribute::Parse(pat_disc::server::CommandLineParameters::GetInstance().GetAttributeConfigFile());

    pat_disc::Timer::GetInstance().Switch("Context Initialization");
    pat_disc::Server::GetInstance().Initialize();
    pat_disc::Timer::GetInstance().Switch("Idle");

    pat_disc::PatientDataServerService service;

    std::vector<std::unique_ptr<grpc::experimental::ServerInterceptorFactoryInterface> > creators;
    creators.push_back(std::make_unique<pat_disc::ServerCostMetricInterceptorFactory>());

    auto credOptions = grpc::SslServerCredentialsOptions(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    credOptions.pem_key_cert_pairs.push_back({pat_disc::ReadFileContents("data/certificates/server-key.pem"), pat_disc::ReadFileContents("data/certificates/server-cert.pem")});
    credOptions.pem_root_certs = pat_disc::ReadFileContents("data/certificates/ca-cert.pem");

    const auto serverCredentials = SslServerCredentials(credOptions);

    // Start gRPC server
    grpc::ServerBuilder builder;
    builder.AddListeningPort("0.0.0.0:50002", serverCredentials);
    builder.RegisterService(&service);
    builder.experimental().SetInterceptorCreators(std::move(creators));
    builder.SetMaxReceiveMessageSize(200 * 1024 * 1024); // 200 MiB

    PD_TRACE("Starting server...");
    const std::unique_ptr server(builder.BuildAndStart());
    server->Wait();
}
