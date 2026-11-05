// Passive HRESULT diagnostics only; no new hook/device/capture behavior.
struct D9ObservedOperation {
 std::atomic<unsigned long long> entries{0},nativeCalls{0},nativeReturns{0},nativeOk{0},nativeFailed{0},returned{0},failed{0};
 std::atomic<HRESULT> last{S_OK},nativeLast{S_OK};
};
D9ObservedOperation g_d9ObservedOperations[5];
struct D9OperationCall {
 D9ObservedOperation& value;
 explicit D9OperationCall(unsigned kind):value(g_d9ObservedOperations[kind]){++value.entries;}
 template<class F>HRESULT Native(F call){++value.nativeCalls;const HRESULT result=call();++value.nativeReturns;value.nativeLast=result;if(SUCCEEDED(result))++value.nativeOk;else ++value.nativeFailed;return result;}
 HRESULT Finish(HRESULT result){++value.returned;value.last=result;if(FAILED(result))++value.failed;return result;}
};
