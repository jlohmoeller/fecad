#ifndef COSTMETRICINTERCEPTOR_HPP
#define COSTMETRICINTERCEPTOR_HPP

#include <grpcpp/support/client_interceptor.h>
#include <grpcpp/support/server_interceptor.h>

namespace pat_disc {
    class CostMetricInterceptor final : public grpc::experimental::Interceptor
    {
    public:
        explicit CostMetricInterceptor() = default;

        void Intercept(grpc::experimental::InterceptorBatchMethods *methods) override;
    };

    class ClientCostMetricInterceptorFactory final : public grpc::experimental::ClientInterceptorFactoryInterface
    {
        grpc::experimental::Interceptor *CreateClientInterceptor(grpc::experimental::ClientRpcInfo *info) override;
    };

    class ServerCostMetricInterceptorFactory final : public grpc::experimental::ServerInterceptorFactoryInterface
    {
        grpc::experimental::Interceptor *CreateServerInterceptor(grpc::experimental::ServerRpcInfo *info) override;
    };
}


#endif //COSTMETRICINTERCEPTOR_HPP
