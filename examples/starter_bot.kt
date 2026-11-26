fun main() {
    val requestId = Regex("\"request_id\"\\s*:\\s*\"([0-9]+)\"")
    for (line in System.`in`.bufferedReader().lineSequence()) {
        val id = requestId.find(line)?.groupValues?.get(1) ?: continue
        println("{\"type\":\"PLAY\",\"request_id\":\"$id\",\"commands\":[\"HARD_DROP\"]}")
        System.out.flush()
    }
}
