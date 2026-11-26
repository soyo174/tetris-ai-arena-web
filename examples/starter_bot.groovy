import groovy.json.JsonOutput
import groovy.json.JsonSlurper

def parser = new JsonSlurper()
System.in.withReader('UTF-8') { reader ->
    reader.eachLine { line ->
        def request = parser.parseText(line)
        println JsonOutput.toJson([
            type: 'PLAY',
            request_id: request.request_id,
            commands: ['HARD_DROP'],
        ])
        System.out.flush()
    }
}
