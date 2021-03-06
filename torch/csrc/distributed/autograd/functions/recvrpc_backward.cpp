#include <torch/csrc/distributed/autograd/functions/recvrpc_backward.h>
#include <ATen/core/functional.h>
#include <torch/csrc/distributed/autograd/rpc_messages/propagate_gradients_req.h>
#include <torch/csrc/distributed/rpc/rpc_agent.h>

namespace torch {
namespace distributed {
namespace autograd {

using torch::autograd::Variable;
using torch::autograd::variable_list;

RecvRpcBackward::RecvRpcBackward(
    const AutogradMetadata& autogradMetadata,
    DistAutogradContext& autogradContext,
    rpc::worker_id_t fromWorkerId)
    : autogradMetadata_(autogradMetadata),
      autogradContext_(autogradContext),
      fromWorkerId_(fromWorkerId) {}

variable_list RecvRpcBackward::apply(variable_list&& grads) {
  std::vector<Variable> outputGrads;
  for (size_t i = 0; i < grads.size(); i++) {
    const auto& grad = grads[i];
    if (grad.defined()) {
      outputGrads.emplace_back(grad);
    } else {
      // Put in zeros for a tensor with no grad.
      outputGrads.emplace_back(input_metadata(i).zeros_like());
    }
  }

  // Send the gradients over the wire and record the future in the autograd
  // context.
  PropagateGradientsReq gradCall(autogradMetadata_, outputGrads);

  // Send the gradients over to the appropriate node (we don't need the worker
  // name only the id, so use a placeholder "foo").
  auto rpcAgent = rpc::RpcAgent::getDefaultRpcAgent();
  auto futureMessage = rpcAgent->send(
      rpcAgent->getWorkerInfo(fromWorkerId_), std::move(gradCall).toMessage());

  // Record the future in the context.
  autogradContext_.addOutstandingRpc(futureMessage);

  // 'recv' function sends the gradients over the wire using RPC, it doesn't
  // need to return anything for any downstream autograd function.
  return variable_list();
}

} // namespace autograd
} // namespace distributed
} // namespace torch
