object Main {
  def main(args: Array[String]): Unit = {
    val requestId = "\"request_id\"\\s*:\\s*\"([0-9]+)\"".r
    for (line <- scala.io.Source.stdin.getLines();
         request <- requestId.findFirstMatchIn(line)) {
      val id = request.group(1)
      println("{\"type\":\"PLAY\",\"request_id\":\"" + id
        + "\",\"commands\":[\"HARD_DROP\"]}")
      Console.out.flush()
    }
  }
}
