
// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <mutex>

#include "arrow/compute/api_vector.h"
#include "arrow/compute/exec.h"
#include "arrow/compute/exec/exec_plan.h"
#include "arrow/compute/exec/expression.h"
#include "arrow/compute/exec/options.h"
#include "arrow/compute/exec/order_by_impl.h"
#include "arrow/compute/exec/util.h"
#include "arrow/compute/exec_internal.h"
#include "arrow/datum.h"
#include "arrow/result.h"
#include "arrow/table.h"
#include "arrow/util/async_generator.h"
#include "arrow/util/async_util.h"
#include "arrow/util/checked_cast.h"
#include "arrow/util/future.h"
#include "arrow/util/logging.h"
#include "arrow/util/optional.h"
#include "arrow/util/thread_pool.h"
#include "arrow/util/tracing_internal.h"
#include "arrow/util/unreachable.h"

namespace arrow {

using internal::checked_cast;

namespace compute {
namespace {

class SinkNode : public ExecNode {
 public:
  SinkNode(ExecPlan* plan, std::vector<ExecNode*> inputs,
           AsyncGenerator<util::optional<ExecBatch>>* generator,
           util::BackpressureOptions backpressure)
      : ExecNode(plan, std::move(inputs), {"collected"}, {},
                 /*num_outputs=*/0),
        producer_(MakeProducer(generator, std::move(backpressure))) {
    DCHECK_EQ(1, inputs_.size());
    output_schema_ = inputs_[0]->output_schema();
  }

  static Result<ExecNode*> Make(ExecPlan* plan, std::vector<ExecNode*> inputs,
                                const ExecNodeOptions& options) {
    RETURN_NOT_OK(ValidateExecNodeInputs(plan, inputs, 1, "SinkNode"));

    const auto& sink_options = checked_cast<const SinkNodeOptions&>(options);
    return plan->EmplaceNode<SinkNode>(plan, std::move(inputs), sink_options.generator,
                                       sink_options.backpressure);
  }

  static PushGenerator<util::optional<ExecBatch>>::Producer MakeProducer(
      AsyncGenerator<util::optional<ExecBatch>>* out_gen,
      util::BackpressureOptions backpressure) {
    PushGenerator<util::optional<ExecBatch>> push_gen(std::move(backpressure));
    auto out = push_gen.producer();
    *out_gen = std::move(push_gen);
    return out;
  }

  const char* kind_name() const override { return "SinkNode"; }

  Status StartProducing() override {
    START_SPAN(span_, std::string(kind_name()) + ":" + label(),
               {{"node.label", label()},
                {"node.detail", ToString()},
                {"node.kind", kind_name()}});
    finished_ = Future<>::Make();
    END_SPAN_ON_FUTURE_COMPLETION(span_, finished_, this);

    return Status::OK();
  }

  // sink nodes have no outputs from which to feel backpressure
  [[noreturn]] static void NoOutputs() {
    Unreachable("no outputs; this should never be called");
  }
  [[noreturn]] void ResumeProducing(ExecNode* output) override { NoOutputs(); }
  [[noreturn]] void PauseProducing(ExecNode* output) override { NoOutputs(); }
  [[noreturn]] void StopProducing(ExecNode* output) override { NoOutputs(); }

  void StopProducing() override {
    EVENT(span_, "StopProducing");
    if (input_counter_.Cancel()) {
      Finish();
    }
  }

  Future<> finished() override { return finished_; }

  void InputReceived(ExecNode* input, ExecBatch batch) override {
    EVENT(span_, "InputReceived", {{"batch.length", batch.length}});
    util::tracing::Span span;
    START_SPAN_WITH_PARENT(span, span_, "InputReceived",
                           {{"node.label", label()}, {"batch.length", batch.length}});

    DCHECK_EQ(input, inputs_[0]);

    bool did_push = producer_.Push(std::move(batch));
    if (!did_push) return;  // producer_ was Closed already

    if (input_counter_.Increment()) {
      Finish();
    }
  }

  void ErrorReceived(ExecNode* input, Status error) override {
    EVENT(span_, "ErrorReceived", {{"error", error.message()}});
    DCHECK_EQ(input, inputs_[0]);

    producer_.Push(std::move(error));

    if (input_counter_.Cancel()) {
      Finish();
    }
  }

  void InputFinished(ExecNode* input, int total_batches) override {
    EVENT(span_, "InputFinished", {{"batches.length", total_batches}});
    if (input_counter_.SetTotal(total_batches)) {
      Finish();
    }
  }

 protected:
  virtual void Finish() {
    producer_.Close();
    finished_.MarkFinished();
  }

  AtomicCounter input_counter_;

  PushGenerator<util::optional<ExecBatch>>::Producer producer_;
};

// A sink node that owns consuming the data and will not finish until the consumption
// is finished.  Use SinkNode if you are transferring the ownership of the data to another
// system.  Use ConsumingSinkNode if the data is being consumed within the exec plan (i.e.
// the exec plan should not complete until the consumption has completed).
class ConsumingSinkNode : public ExecNode {
 public:
  ConsumingSinkNode(ExecPlan* plan, std::vector<ExecNode*> inputs,
                    std::shared_ptr<SinkNodeConsumer> consumer)
      : ExecNode(plan, std::move(inputs), {"to_consume"}, {},
                 /*num_outputs=*/0),
        consumer_(std::move(consumer)) {}

  static Result<ExecNode*> Make(ExecPlan* plan, std::vector<ExecNode*> inputs,
                                const ExecNodeOptions& options) {
    RETURN_NOT_OK(ValidateExecNodeInputs(plan, inputs, 1, "SinkNode"));

    const auto& sink_options = checked_cast<const ConsumingSinkNodeOptions&>(options);
    return plan->EmplaceNode<ConsumingSinkNode>(plan, std::move(inputs),
                                                std::move(sink_options.consumer));
  }

  const char* kind_name() const override { return "ConsumingSinkNode"; }

  Status StartProducing() override {
    START_SPAN(span_, std::string(kind_name()) + ":" + label(),
               {{"node.label", label()},
                {"node.detail", ToString()},
                {"node.kind", kind_name()}});
    finished_ = Future<>::Make();
    END_SPAN_ON_FUTURE_COMPLETION(span_, finished_, this);
    return Status::OK();
  }

  // sink nodes have no outputs from which to feel backpressure
  [[noreturn]] static void NoOutputs() {
    Unreachable("no outputs; this should never be called");
  }
  [[noreturn]] void ResumeProducing(ExecNode* output) override { NoOutputs(); }
  [[noreturn]] void PauseProducing(ExecNode* output) override { NoOutputs(); }
  [[noreturn]] void StopProducing(ExecNode* output) override { NoOutputs(); }

  void StopProducing() override {
    EVENT(span_, "StopProducing");
    if (input_counter_.Cancel()) {
      Finish(Status::OK());
    }
  }

  Future<> finished() override { return finished_; }

  void InputReceived(ExecNode* input, ExecBatch batch) override {
    EVENT(span_, "InputReceived", {{"batch.length", batch.length}});
    util::tracing::Span span;
    START_SPAN_WITH_PARENT(span, span_, "InputReceived",
                           {{"node.label", label()}, {"batch.length", batch.length}});

    DCHECK_EQ(input, inputs_[0]);

    // This can happen if an error was received and the source hasn't yet stopped.  Since
    // we have already called consumer_->Finish we don't want to call consumer_->Consume
    if (input_counter_.Completed()) {
      return;
    }

    Status consumption_status = consumer_->Consume(std::move(batch));
    if (!consumption_status.ok()) {
      if (input_counter_.Cancel()) {
        Finish(std::move(consumption_status));
      }
      return;
    }

    if (input_counter_.Increment()) {
      Finish(Status::OK());
    }
  }

  void ErrorReceived(ExecNode* input, Status error) override {
    EVENT(span_, "ErrorReceived", {{"error", error.message()}});
    DCHECK_EQ(input, inputs_[0]);

    if (input_counter_.Cancel()) {
      Finish(std::move(error));
    }
  }

  void InputFinished(ExecNode* input, int total_batches) override {
    EVENT(span_, "InputFinished", {{"batches.length", total_batches}});
    if (input_counter_.SetTotal(total_batches)) {
      Finish(Status::OK());
    }
  }

 protected:
  virtual void Finish(const Status& finish_st) {
    consumer_->Finish().AddCallback([this, finish_st](const Status& st) {
      // Prefer the plan error over the consumer error
      Status final_status = finish_st & st;
      finished_.MarkFinished(std::move(final_status));
    });
  }

  AtomicCounter input_counter_;

  std::shared_ptr<SinkNodeConsumer> consumer_;
};

/**
 * @brief This node is an extension on ConsumingSinkNode
 * to facilitate to get the output from an execution plan
 * as a table. We define a custom SinkNodeConsumer to
 * enable this functionality.
 */

struct TableSinkNodeConsumer : public arrow::compute::SinkNodeConsumer {
 public:
  TableSinkNodeConsumer(std::shared_ptr<Table>* out,
                        std::shared_ptr<Schema> output_schema, MemoryPool* pool)
      : out_(out), output_schema_(std::move(output_schema)), pool_(pool) {}

  Status Consume(ExecBatch batch) override {
    std::lock_guard<std::mutex> guard(consume_mutex_);
    ARROW_ASSIGN_OR_RAISE(auto rb, batch.ToRecordBatch(output_schema_, pool_));
    batches_.push_back(rb);
    return Status::OK();
  }

  Future<> Finish() override {
    ARROW_ASSIGN_OR_RAISE(*out_, Table::FromRecordBatches(batches_));
    return Status::OK();
  }

 private:
  std::shared_ptr<Table>* out_;
  std::shared_ptr<Schema> output_schema_;
  MemoryPool* pool_;
  std::vector<std::shared_ptr<RecordBatch>> batches_;
  std::mutex consume_mutex_;
};

static Result<ExecNode*> MakeTableConsumingSinkNode(
    compute::ExecPlan* plan, std::vector<compute::ExecNode*> inputs,
    const compute::ExecNodeOptions& options) {
  RETURN_NOT_OK(ValidateExecNodeInputs(plan, inputs, 1, "TableConsumingSinkNode"));
  const auto& sink_options = checked_cast<const TableSinkNodeOptions&>(options);
  MemoryPool* pool = plan->exec_context()->memory_pool();
  auto tb_consumer = std::make_shared<TableSinkNodeConsumer>(
      sink_options.output_table, sink_options.output_schema, pool);
  auto consuming_sink_node_options = ConsumingSinkNodeOptions{tb_consumer};
  return MakeExecNode("consuming_sink", plan, inputs, consuming_sink_node_options);
}

// A sink node that accumulates inputs, then sorts them before emitting them.
struct OrderBySinkNode final : public SinkNode {
  OrderBySinkNode(ExecPlan* plan, std::vector<ExecNode*> inputs,
                  std::unique_ptr<OrderByImpl> impl,
                  AsyncGenerator<util::optional<ExecBatch>>* generator,
                  util::BackpressureOptions backpressure)
      : SinkNode(plan, std::move(inputs), generator, std::move(backpressure)),
        impl_{std::move(impl)} {}

  const char* kind_name() const override { return "OrderBySinkNode"; }

  // A sink node that accumulates inputs, then sorts them before emitting them.
  static Result<ExecNode*> MakeSort(ExecPlan* plan, std::vector<ExecNode*> inputs,
                                    const ExecNodeOptions& options) {
    RETURN_NOT_OK(ValidateExecNodeInputs(plan, inputs, 1, "OrderBySinkNode"));

    const auto& sink_options = checked_cast<const OrderBySinkNodeOptions&>(options);
    ARROW_ASSIGN_OR_RAISE(
        std::unique_ptr<OrderByImpl> impl,
        OrderByImpl::MakeSort(plan->exec_context(), inputs[0]->output_schema(),
                              sink_options.sort_options));
    return plan->EmplaceNode<OrderBySinkNode>(plan, std::move(inputs), std::move(impl),
                                              sink_options.generator,
                                              sink_options.backpressure);
  }

  // A sink node that receives inputs and then compute top_k/bottom_k.
  static Result<ExecNode*> MakeSelectK(ExecPlan* plan, std::vector<ExecNode*> inputs,
                                       const ExecNodeOptions& options) {
    RETURN_NOT_OK(ValidateExecNodeInputs(plan, inputs, 1, "OrderBySinkNode"));

    const auto& sink_options = checked_cast<const SelectKSinkNodeOptions&>(options);
    ARROW_ASSIGN_OR_RAISE(
        std::unique_ptr<OrderByImpl> impl,
        OrderByImpl::MakeSelectK(plan->exec_context(), inputs[0]->output_schema(),
                                 sink_options.select_k_options));
    return plan->EmplaceNode<OrderBySinkNode>(plan, std::move(inputs), std::move(impl),
                                              sink_options.generator,
                                              sink_options.backpressure);
  }

  void InputReceived(ExecNode* input, ExecBatch batch) override {
    EVENT(span_, "InputReceived", {{"batch.length", batch.length}});
    util::tracing::Span span;
    START_SPAN_WITH_PARENT(span, span_, "InputReceived",
                           {{"node.label", label()}, {"batch.length", batch.length}});

    DCHECK_EQ(input, inputs_[0]);

    auto maybe_batch = batch.ToRecordBatch(inputs_[0]->output_schema(),
                                           plan()->exec_context()->memory_pool());
    if (ErrorIfNotOk(maybe_batch.status())) {
      StopProducing();
      if (input_counter_.Cancel()) {
        finished_.MarkFinished(maybe_batch.status());
      }
      return;
    }
    auto record_batch = maybe_batch.MoveValueUnsafe();

    impl_->InputReceived(std::move(record_batch));
    if (input_counter_.Increment()) {
      Finish();
    }
  }

 protected:
  Status DoFinish() {
    ARROW_ASSIGN_OR_RAISE(Datum sorted, impl_->DoFinish());
    TableBatchReader reader(*sorted.table());
    while (true) {
      std::shared_ptr<RecordBatch> batch;
      RETURN_NOT_OK(reader.ReadNext(&batch));
      if (!batch) break;
      bool did_push = producer_.Push(ExecBatch(*batch));
      if (!did_push) break;  // producer_ was Closed already
    }
    return Status::OK();
  }

  void Finish() override {
    util::tracing::Span span;
    START_SPAN_WITH_PARENT(span, span_, "Finish", {{"node.label", label()}});
    Status st = DoFinish();
    if (ErrorIfNotOk(st)) {
      producer_.Push(std::move(st));
    }
    SinkNode::Finish();
  }

 protected:
  std::string ToStringExtra(int indent = 0) const override {
    return std::string("by=") + impl_->ToString();
  }

 private:
  std::unique_ptr<OrderByImpl> impl_;
};

}  // namespace

namespace internal {

void RegisterSinkNode(ExecFactoryRegistry* registry) {
  DCHECK_OK(registry->AddFactory("select_k_sink", OrderBySinkNode::MakeSelectK));
  DCHECK_OK(registry->AddFactory("order_by_sink", OrderBySinkNode::MakeSort));
  DCHECK_OK(registry->AddFactory("consuming_sink", ConsumingSinkNode::Make));
  DCHECK_OK(registry->AddFactory("sink", SinkNode::Make));
  DCHECK_OK(registry->AddFactory("table_sink", MakeTableConsumingSinkNode));
}

}  // namespace internal
}  // namespace compute
}  // namespace arrow
