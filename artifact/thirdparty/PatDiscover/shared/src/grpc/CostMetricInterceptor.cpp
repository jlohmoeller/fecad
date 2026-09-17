#include "pdshared/grpc/CostMetricInterceptor.hpp"

#include <pdshared/measure/CommunicationCost.hpp>

namespace pat_disc {
    void CostMetricInterceptor::Intercept(grpc::experimental::InterceptorBatchMethods *methods)
    {
        if (methods->QueryInterceptionHookPoint(grpc::experimental::InterceptionHookPoints::PRE_SEND_MESSAGE))
        {
            CommunicationCost::GetInstance().RecordMessageSize(methods->GetSerializedSendMessage()->Length());
        }

        methods->Proceed();
    }

    grpc::experimental::Interceptor *ClientCostMetricInterceptorFactory::CreateClientInterceptor(grpc::experimental::ClientRpcInfo *info)
    {
        return new CostMetricInterceptor();
    }

    grpc::experimental::Interceptor *ServerCostMetricInterceptorFactory::CreateServerInterceptor(grpc::experimental::ServerRpcInfo *info)
    {
        return new CostMetricInterceptor();
    }
}
