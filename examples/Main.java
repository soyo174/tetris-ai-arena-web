import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.regex.Pattern;

public class Main {
    public static void main(String[] args) throws Exception {
        var requestId = Pattern.compile("\"request_id\"\\s*:\\s*\"([0-9]+)\"");
        var input = new BufferedReader(new InputStreamReader(System.in, StandardCharsets.UTF_8));
        for (String line; (line = input.readLine()) != null; ) {
            var match = requestId.matcher(line);
            if (!match.find()) continue;
            System.out.println("{\"type\":\"PLAY\",\"request_id\":\"" + match.group(1)
                    + "\",\"commands\":[\"HARD_DROP\"]}");
            System.out.flush();
        }
    }
}
